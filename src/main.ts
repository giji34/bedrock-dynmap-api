import type { Request, Response } from "express";
import * as express from "express";
import { Cli } from "./cli";
import { Monitor, MonitorOptions } from "./monitor";
import { Dimension } from "./types";
import caporal = require("caporal");

caporal
  .command("run", "")
  .option("--executable <path>", "", caporal.STRING, undefined, true)
  .option("--port <port>", "", caporal.INTEGER, 3000, false)
  .option(
    "--inspector-entity-name-overworld <player>",
    "",
    caporal.STRING,
    undefined,
    false
  )
  .option(
    "--inspector-entity-name-nether <player>",
    "",
    caporal.STRING,
    undefined,
    false
  )
  .option(
    "--inspector-entity-name-the-end <player>",
    "",
    caporal.STRING,
    undefined,
    false
  )
  .action(async (args, options) => {
    const port = options["port"];
    const executable = options["executable"];

    const inspectors = new Map<Dimension, string>();
    const overworld = options["inspectorEntityNameOverworld"];
    const nether = options["inspectorEntityNameNether"];
    const theEnd = options["inspectorEntityNameTheEnd"];
    if (overworld) {
      inspectors.set("overworld", overworld);
    }
    if (nether) {
      inspectors.set("nether", nether);
    }
    if (theEnd) {
      inspectors.set("the_end", theEnd);
    }
    const monitorOptions: MonitorOptions = { inspectors };

    const cli = new Cli(executable);
    await cli.start();
    const monitor = new Monitor(cli, monitorOptions);
    monitor.start();

    const app = express();
    app.get("/players", (req: Request, res: Response) => {
      try {
        const players = monitor.players;
        res.status(200);
        res.json({ status: "ok", players });
      } catch (e) {
        res.status(500);
        res.json({ status: "error" });
      }
    });
    app.listen(port, () => {
      console.log(`[INSPECTOR] started at port ${port}`);
    });
  });

caporal.parse(process.argv);