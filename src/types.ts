export type Player = {
  name: string;
  location: PlayerLocation;
};

export type Point = { x: number; z: number };
export type Size = { width: number; height: number };
export type Rect = Point & Size;
export type PlayerLocation = Point & { dimension: Dimension; accuracy: number };
export type Dimension = "overworld" | "nether" | "the_end";

export type Command = {
  dimension: Dimension;
  command: string;
  interval: number; // milliseconds
};
export type CommandSetting = {
  commands: Command[];
};
