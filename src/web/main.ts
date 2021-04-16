import type { Request, Response, Errback } from "express";
import * as express from "express";
import * as child_process from "child_process";
import * as path from "path";
import helmet = require("helmet");
import * as process from "process";
import * as caporal from "caporal";
import * as fetch from "node-fetch";

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

type Player = {
  world: string;
  armor: number;
  name: string;
  x: number;
  y: number;
  health: number;
  z: number;
  sort: number;
  type: string;
  account: string;
};

type Report = {
  currentcount: number;
  hasStorm: boolean;
  players: Player[];
  isThundering: boolean;
  confighash: number;
  servertime: number;
  updates: any[];
  timestamp: number;
};

async function startWebServer(params: {
  port: number;
  base: string;
}): Promise<void> {
  const { port, base } = params;
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
    app.get("*", (req: Request, res: Response, next: Errback) => {
      if (report === "") {
        res.status(404);
        res.send("");
        return;
      }
      let p = req.path;
      while (p.startsWith("/")) {
        p = p.substr(1);
      }
      const elements = p.split("/");
      if (elements.length !== 2) {
        res.status(400);
        res.send("");
        return;
      }
      const world = elements[0];
      const timestamp = elements[1];
      if (
        world !== "world" &&
        world !== "world_nether" &&
        world !== "world_the_end"
      ) {
        res.status(400);
        res.send("");
        return;
      }
      if (isNaN(parseInt(timestamp, 10))) {
        res.status(400);
        res.send("");
        return;
      }
      let r: Report;
      try {
        r = JSON.parse(report) as Report;
      } catch (e) {
        next(e);
        return;
      }
      fetch(`${base}/up/world/${world}/${timestamp}`)
        .then((res) => res.json() as Report)
        .then((o) => {
          const n: Report = {
            ...r,
            updates: o.updates,
            confighash: o.confighash,
          };
          res.status(200);
          res.json(n);
        })
        .catch(next);
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
  .option("--base <url>", "url of actual dynmap server", caporal.STRING)
  .action(async (args, opts) => {
    const port = opts.port;
    const base = opts.base;
    const exe = opts.exe;
    const pid = await startGameServer(path.resolve(exe));
    await startTracer(pid);
    await startWebServer({ port, base });
  });

caporal.parse(process.argv);
