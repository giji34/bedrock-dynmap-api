import { Cli } from "./cli";
import { Dimension, Player, PlayerLocation, Point, Rect } from "./types";

export type MonitorOptions = {
  inspectors: Map<Dimension, string>;
};

export class Monitor {
  private _players: Player[] = [];

  constructor(
    private readonly cli: Cli,
    private readonly options: MonitorOptions
  ) {}

  start() {
    this.update();
  }

  get players(): Player[] {
    return JSON.parse(JSON.stringify(this._players)) as Player[];
  }

  private readonly update = () => {
    this.searchOnlinePlayers()
      .catch(console.error)
      .finally(() => {
        setTimeout(this.update, 500);
      });
  };

  private async getOnlinePlayers(
    includeInspectorPlayer: boolean = false
  ): Promise<string[]> {
    const listResult = await this.cli.exec("list");
    const lines = listResult
      .split("\n")
      .splice(1)
      .filter((p) => p.length > 0);
    let players: string[] = [];
    for (const line of lines) {
      const values = line
        .trim()
        .split(",")
        .map((v) => v.trim())
        .filter((v) => v.length > 0);
      players.push(...values);
    }
    if (!includeInspectorPlayer) {
      for (const inspector in this.options.inspectors.values()) {
        players = players.filter((p) => p !== inspector);
      }
    }
    return players;
  }

  private async searchOnlinePlayers(): Promise<void> {
    const players = await this.getOnlinePlayers();
    const result: Player[] = [];
    for (const player of players) {
      const location = await this.inspectPlayerLocation(player);
      if (location) {
        result.push({ name: player, location });
      }
    }
    this._players = result;
  }

  private async inspectPlayerDimension(name): Promise<Dimension | undefined> {
    for (const dimension of ["overworld", "nether", "the_end"] as Dimension[]) {
      const inspector = this.options.inspectors.get(dimension);
      if (!inspector) {
        continue;
      }
      const command = `execute @e[name=${inspector}] ^ ^ ^ testfor @a[name=${name},x=0,y=0,z=0,r=99999]`;
      const result = await this.cli.exec(command);
      if (result.startsWith(`Found ${name}`)) {
        return dimension;
      }
    }
    return undefined;
  }

  async isPlayerInRegion(params: {
    name: string;
    dimension: Dimension;
    rect: Rect;
  }): Promise<boolean | undefined> {
    const { name, dimension, rect } = params;
    const inspector = this.options.inspectors.get(dimension);
    if (!inspector) {
      return undefined;
    }
    const command = `execute @e[name=${inspector}] ^ ^ ^ testfor @a[name=${name},x=${rect.x},y=0,z=${rect.z},dx=${rect.width},dy=2000,dz=${rect.height}]`;
    const result = await this.cli.exec(command);
    return result.startsWith(`Found ${name}`);
  }

  private async inspectPlayerLocation(
    name: string,
    hint?: PlayerLocation
  ): Promise<PlayerLocation | undefined> {
    const dimension = await this.inspectPlayerDimension(name);
    if (!dimension) {
      return undefined;
    }
    let pivot: Point = { x: 0, z: 0 };
    let size = 2000;
    if (hint) {
      pivot = { x: hint.x, z: hint.z };
      size = hint.accuracy * 4;
    }
    let itr = 0;
    const targetAccuracy = 4;
    const isIn = async (rect: Rect): Promise<boolean | undefined> => {
      return this.isPlayerInRegion({ name, dimension, rect });
    };
    let ok = false;
    while (itr < 32 && size > targetAccuracy) {
      itr++;
      const rect: Rect = {
        x: pivot.x - size * 0.5,
        z: pivot.z - size * 0.5,
        width: size,
        height: size,
      };

      if ((await isIn(rect)) === true) {
        const nextSize = Math.ceil(size * 0.5);
        const delta: Point[] = [
          { x: -1, z: -1 }, // north west
          { x: 1, z: -1 }, // north east
          { x: 1, z: 1 }, // south east
          { x: -1, z: 1 }, // south west
        ];
        for (const d of delta) {
          const nextPivot: Point = {
            x: Math.floor(pivot.x + size * 0.25 * d.x),
            z: Math.floor(pivot.z + size * 0.25 * d.z),
          };
          const r: Rect = {
            x: Math.floor(nextPivot.x - nextSize * 0.5),
            z: Math.floor(nextPivot.z - nextSize * 0.5),
            width: Math.ceil(nextSize),
            height: Math.ceil(nextSize),
          };
          const found = await isIn(r);
          if (found === true) {
            pivot = nextPivot;
            size = Math.ceil(size * 0.5);
            ok = true;
            break;
          }
        }
        if (ok && size === 1) {
          break;
        }
      } else {
        size = size * 2;
      }
    }

    if (ok) {
      return { dimension, ...pivot, accuracy: size };
    } else {
      return undefined;
    }
  }
}
