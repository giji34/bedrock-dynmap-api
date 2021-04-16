import type { Request, Response } from "express";
import * as express from "express";
import * as child_process from "child_process";
import * as path from "path";
import helmet = require("helmet");
import * as process from "process";
import * as caporal from "caporal";

async function startGameServer(exe: string): Promise<number> {
  return new Promise((resolve) => {
    const cwd = path.dirname(exe);
    const game = child_process.spawn(exe, {
      cwd,
      stdio: ["inherit", "pipe", "inherit"],
    });
    game.stdout.on("data", (chunk: Buffer) => {
      const lines = chunk.toString("utf-8");
      lines.split("\n").forEach((line) => {
        if (line === "") {
          return;
        }
        console.log(`[game] ${line}`);
        if (line === "[INFO] Server started.") {
          resolve(game.pid);
        }
      });
    });
    game.on("exit", (code: number) => {
      process.exit(code);
    });
  });
}

let report: string = "";

async function startTracer(pid: number): Promise<void> {
  const exe = path.join(__dirname, "/../tracer/tracer");
  const tracer = child_process.spawn("sudo", [exe, `${pid}`], {
    stdio: "pipe",
  });
  tracer.stdout.on("data", (chunk: Buffer) => {
    const lines = chunk
      .toString("utf-8")
      .split("\n")
      .map((l) => l.trim())
      .filter((l) => l !== "");
    if (lines.length > 0) {
      report = lines[lines.length - 1];
    }
  });
  tracer.on("error", (e) => {
    console.log(e);
  });
}

async function startWebServer(port: number): Promise<void> {
  return new Promise((resolve) => {
    const app = express();
    app.use(helmet());
    app.use(
      helmet.contentSecurityPolicy({
        directives: {
          defaultSrc: ["'self'"],
        },
      })
    );
    app.get("/up/*", (req: Request, res: Response) => {
      if (report === "") {
        res.status(404);
        res.send("");
      } else {
        res.status(200);
        res.send(report);
      }
    });
    app.listen(port, () => {
      console.log(`[web] started at port ${port}`);
      resolve();
    });
  });
}

caporal
  .command("run", "start server")
  .option("--exe <exe>", "bedrock server executable", caporal.STRING)
  .option("--port <port>", "port number of web server", caporal.INT, 3000)
  .action(async (args, opts) => {
    const port = opts.port;
    const exe = opts.exe;
    const pid = await startGameServer(path.resolve(exe));
    await startTracer(pid);
    await startWebServer(port);
  });

caporal.parse(process.argv);
