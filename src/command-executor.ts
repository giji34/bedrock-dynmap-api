import { CommandSetting } from "./types";
import { Cli } from "./cli";
import { InspectorOptions } from "./inspector";

export class CommandExecutor {
  constructor(
    private readonly cli: Cli,
    private readonly options: InspectorOptions,
    private readonly setting: CommandSetting
  ) {}

  start() {
    this.setting.commands.forEach((command) => {
      const inspector = this.options.inspectors.get(command.dimension)!;
      if (!inspector) {
        console.error(
          `[COMMAND_EXECUTOR] inspector not found for dimension: ${command.dimension}`
        );
        return;
      }
      setInterval(() => {
        this.cli
          .exec(`execute @e[name=${inspector}] 0 0 0 ${command.command}`)
          .catch(console.error);
      }, command.interval);
    });
  }
}
