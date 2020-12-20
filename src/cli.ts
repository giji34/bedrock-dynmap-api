import * as child_process from "child_process";
import { ChildProcess } from "child_process";
import * as path from "path";

export class Cli {
  private p: ChildProcess;

  constructor(private readonly executable: string) {}

  async start(): Promise<void> {
    const { executable } = this;
    const cwd = path.dirname(executable);
    const env = {
      LD_LIBRARY_PATH: ".",
    };
    this.p = child_process.spawn(executable, {
      cwd,
      env,
    });
    process.on("SIGINT", this.handleSignal);
    process.on("SIGTERM", this.handleSignal);
    this.p.on("exit", (code: number) => {
      console.log(`bds exit with code: ${code}`);
      process.removeListener("SIGINT", this.handleSignal);
      process.removeListener("SIGTERM", this.handleSignal);
      process.exit(code);
    });
    this.p.stdout!.addListener("data", this.defaultStdout);
    process.stdin.pipe(this.p.stdin!);

    return new Promise((resolve, reject) => {
      let initLog = "";
      const onInitLog = (data) => {
        initLog += data.toString("utf-8");
        if (initLog.endsWith("[INFO] Server started.\n")) {
          this.p.stdout!.removeListener("data", onInitLog);
          resolve();
          this.runLoop();
        }
      };
      this.p.stdout!.addListener("data", onInitLog);
    });
  }

  private readonly runLoop = () => {
    this.unsafeRunLoop()
      .catch(console.error)
      .finally(() => {
        setTimeout(this.runLoop, 10);
      });
  };

  private async unsafeRunLoop(): Promise<void> {
    if (this.queue.length === 0) {
      return;
    }
    const [q] = this.queue.splice(0, 1);
    return new Promise((resolve, reject) => {
      let result = "";
      const onData = (data) => {
        result += data.toString("utf-8");
        q.then(result);
        resolve();
        this.p.stdout!.removeListener("data", onData);
        this.p.stdout!.addListener("data", this.defaultStdout);
      };
      this.p.stdout!.removeListener("data", this.defaultStdout);
      this.p.stdout!.addListener("data", onData);
      this.p.stdin!.write(q.command + "\n");
    });
  }

  private readonly handleSignal = (sig) => {
    this.p.kill(sig);
    process.exit(1);
  };

  private readonly defaultStdout = (data) => {
    let str = data.toString("utf-8");
    if (str.endsWith("\n")) {
      str = str.substr(0, str.length - 1);
    }
    console.log(str);
  };

  private queue: Queue[] = [];

  async exec(command: string): Promise<string> {
    return new Promise((resolve, reject) => {
      this.queue.push({
        command,
        then: (result: string) => {
          resolve(result);
        },
      });
    });
  }
}

type Queue = {
  command: string;
  then: (result: string) => void;
};
