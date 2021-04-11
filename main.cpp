#include <asm/unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>

struct Vec3 {
  float x;
  float y;
  float z;
};

enum class Dimension : int {
  NORMAL = 0,
  NETHER = 1,
  THE_END = 2,
};

static std::string StringFromDimension(Dimension d) {
  switch (d) {
  case Dimension::NORMAL:
    return "normal";
  case Dimension::NETHER:
    return "nether";
  case Dimension::THE_END:
    return "the_end";
  default:
    return "";
  }
}

struct Location {
  Dimension fDimension;
  Vec3 fPos;

  std::string toString() const {
    return "[" + std::to_string(fPos.x) + ", " + std::to_string(fPos.y) + ", " + std::to_string(fPos.z) + "]@" + StringFromDimension(fDimension);
  }
};

class Player {
public:
  explicit Player(void *address) : fAddress(address) {
  }

  std::optional<Location> location() const {
    if (!fPos || !fDimension) {
      return std::nullopt;
    }
    Location l;
    l.fDimension = *fDimension;
    l.fPos = *fPos;
    return l;
  }

  std::optional<Vec3> pos() const {
    return fPos;
  }

  void setPos(Vec3 p) {
    fPos = p;
  }

  std::optional<Vec3> move(Vec3 delta) {
    if (!fPos) {
      return std::nullopt;
    }
    Vec3 n = *fPos;
    n.x += delta.x;
    n.z += delta.z;
    fPos = n;
    return n;
  }

  std::optional<Dimension> dimension() const {
    return fDimension;
  }

  void setDimension(int dimension) {
    if (dimension < 0 || 2 < dimension) {
      return;
    }
    fDimension = static_cast<Dimension>(dimension);
  }

public:
  std::string fName;
  void *fAddress;

private:
  std::optional<Vec3> fPos;
  std::optional<Dimension> fDimension;
};

class PlayerRegistry {
public:
  Player *unsafeGetByAddress(void *address) const {
    auto found = fPlayers.find(address);
    if (found == fPlayers.end()) {
      return nullptr;
    }
    return found->second.get();
  }

  Player *getByAddress(void *address) {
    auto found = fPlayers.find(address);
    if (found == fPlayers.end()) {
      auto p = std::make_shared<Player>(address);
      fPlayers.insert(std::make_pair(address, p));
      return p.get();
    }
    return found->second.get();
  }

  Player *getByName(std::string const &name, void *newAddress) {
    for (auto it : fPlayers) {
      auto const &player = it.second;
      if (player->fName == name) {
        if (player->fAddress != newAddress) {
          fPlayers.erase(player->fAddress);
          player->fAddress = newAddress;
          fPlayers.insert(std::make_pair(newAddress, player));
        }
        return player.get();
      }
    }
    auto p = std::make_shared<Player>(newAddress);
    p->fName = name;
    fPlayers.insert(std::make_pair(newAddress, p));
    return p.get();
  }

  void each(std::function<void(Player const &player)> callback) {
    for (auto it : fPlayers) {
      Player const *player = it.second.get();
      if (player->location() && !player->fName.empty()) {
        callback(*player);
      }
    }
  }

  void forget(Player *player) {
    if (!player) {
      return;
    }
    fPlayers.erase(player->fAddress);
  }

private:
  std::map<void *, std::shared_ptr<Player>> fPlayers;
};

PlayerRegistry sPlayers;

using HookCallback = std::function<void(pid_t, struct user_regs_struct)>;

struct Hook {
  unsigned long fAddress;
  HookCallback fCallback;
  long fOriginalInstruction;
};

void AttachAllThread(int pid) {
  char _taskdir[255];

  sprintf(_taskdir, "/proc/%d/task", pid);

  DIR *taskdir = opendir(_taskdir);
  if (!taskdir) {
    return;
  }

  struct dirent *d = readdir(taskdir);
  while (d) {
    int tid = atoi(d->d_name);
    int status;
    ptrace(PTRACE_ATTACH, tid, 0, 0);
    int id = waitpid(-1, &status, __WALL);
    ptrace(PTRACE_CONT, id, 0, 0);
    d = readdir(taskdir);
  }
  closedir(taskdir);
}

static bool Read(pid_t pid, void *address, void *buffer, size_t size) {
  struct iovec lvec;
  lvec.iov_base = buffer;
  lvec.iov_len = size;
  struct iovec rvec;
  rvec.iov_base = address;
  rvec.iov_len = size;
  return process_vm_readv(pid, &lvec, 1, &rvec, 1, 0) == size;
}

static std::optional<Vec3> ReadVec3(pid_t pid, void *address) {
  Vec3 v;
  if (Read(pid, address, &v, sizeof(v))) {
    return v;
  } else {
    return std::nullopt;
  }
}

static std::optional<std::string> ReadString(pid_t pid, void *address) {
  struct AllocHider : std::allocator<char> {
    void *fPointer;
  };
  struct String {
    AllocHider fHider;
    size_t fSize;
  };
  String s;
  if (!Read(pid, address, &s, sizeof(String))) {
    return std::nullopt;
  }
  std::vector<char> data(s.fSize);
  data.push_back(0);
  if (!Read(pid, s.fHider.fPointer, data.data(), s.fSize)) {
    return std::nullopt;
  }
  return std::string(data.data());
}

// Actor::setPos(Vec3 const&)
static void HookActorSetPos(pid_t pid, struct user_regs_struct regs) {
  auto player = sPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  auto pos = ReadVec3(pid, (void *)regs.rsi);
  if (!pos) {
    return;
  }
  player->setPos(*pos);
}

// Player::move(Vec3 const&)
static void HookPlayerMove(pid_t pid, struct user_regs_struct regs) {
  auto address = (void *)regs.rdi;
  auto player = sPlayers.unsafeGetByAddress(address);
  if (!player) {
    return;
  }
  auto delta = ReadVec3(pid, (void *)regs.rsi);
  if (!delta) {
    return;
  }
  player->move(*delta);
}

// Player::setName(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)
static void HookPlayerSetName(pid_t pid, struct user_regs_struct regs) {
  auto address = (void *)regs.rdi;
  auto name = ReadString(pid, (void *)regs.rsi);
  if (!name) {
    return;
  }
  sPlayers.getByName(*name, address);
}

// ServerPlayer::changeDimension(AutomaticID<Dimension, int>, bool)
// ServerPlayer::changeDimensionWithCredits(AutomaticID<Dimension, int>)
static void HookServerPlayerChangeDimension(pid_t pid, struct user_regs_struct regs) {
  auto player = sPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  int dimension = (int)regs.rsi;
  player->setDimension(dimension);
}

// ServerPlayer::is2DPositionRelevant(AutomaticID<Dimension, int>, BlockPos const&)
static void HookServerPlayerIs2DPositionRelevant(pid_t pid, struct user_regs_struct regs) {
  auto player = sPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  int dimension = (int)regs.rsi;
  player->setDimension(dimension);
}

// ServerPlayer::~ServerPlayer()
static void HookServerPlayerDestructor(pid_t pid, struct user_regs_struct regs) {
  auto player = sPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  sPlayers.forget(player);
}

static void HookDebug(pid_t pid, struct user_regs_struct regs) {
  printf("%s\n", __FUNCTION__);
}

int main(int argc, char *argv[]) {
  pid_t pid = atoi(argv[1]);

  Hook hooks[] = {
      {.fAddress = 0x0000000001f9fbd0, .fCallback = HookActorSetPos},
      {.fAddress = 0x0000000001b172b0, .fCallback = HookPlayerMove},
      {.fAddress = 0x0000000001b14270, .fCallback = HookPlayerSetName},
      {.fAddress = 0x00000000016ac180, .fCallback = HookServerPlayerChangeDimension},
      {.fAddress = 0x00000000016ac290, .fCallback = HookServerPlayerChangeDimension},
      {.fAddress = 0x00000000016ac970, .fCallback = HookServerPlayerIs2DPositionRelevant},
      {.fAddress = 0x00000000016a46c0, .fCallback = HookServerPlayerDestructor},
      {.fAddress = 0x00000000016a4530, .fCallback = HookServerPlayerDestructor},
  };
  size_t const kNumHooks = sizeof(hooks) / sizeof(Hook);

  AttachAllThread(pid);

  syscall(__NR_tkill, pid, SIGSTOP);
  int s;
  waitpid(pid, &s, 0);

  long const kInt3Opcode = 0x000000CC;
  for (size_t i = 0; i < kNumHooks; i++) {
    unsigned long address = hooks[i].fAddress;
    long original = ptrace(PTRACE_PEEKTEXT, pid, address, nullptr);
    ;
    hooks[i].fOriginalInstruction = original;
    ptrace(PTRACE_POKETEXT, pid, address, ((original & 0xFFFFFFFFFFFFFF00) | kInt3Opcode));
  }

  ptrace(PTRACE_CONT, pid, NULL, NULL);

  int status;
  while (true) {
    int tid = waitpid(-1, &status, __WALL);

    if (WIFEXITED(status)) {
      exit(0);
    }

    if (!WIFSTOPPED(status)) {
      exit(1);
    }

    int signum = WSTOPSIG(status);
    if (signum == SIGTRAP) {
      struct user_regs_struct regs;
      ptrace(PTRACE_GETREGS, tid, 0, &regs);

      Hook target;
      for (size_t i = 0; i < kNumHooks; i++) {
        if (hooks[i].fAddress + 1 == regs.rip) {
          target = hooks[i];
          break;
        }
      }

      target.fCallback(pid, regs);

      // for debug
      sPlayers.each([](Player const &player) {
        printf("%s %s\n", player.fName.c_str(), player.location()->toString().c_str());
      });

      ptrace(PTRACE_POKETEXT, tid, target.fAddress, target.fOriginalInstruction);
      regs.rip--;
      ptrace(PTRACE_SETREGS, tid, 0, &regs);

      ptrace(PTRACE_SINGLESTEP, tid, 0, 0);
      waitpid(tid, &status, __WALL);
      ptrace(PTRACE_POKETEXT, tid, target.fAddress, ((target.fOriginalInstruction & 0xFFFFFFFFFFFFFF00) | kInt3Opcode));

      ptrace(PTRACE_CONT, tid, 0, 0);
    } else if (signum == 19 || signum == 21) {
      ptrace(PTRACE_CONT, tid, 0, 0);
    } else {
      ptrace(PTRACE_CONT, tid, 0, signum);
    }
  }
}
