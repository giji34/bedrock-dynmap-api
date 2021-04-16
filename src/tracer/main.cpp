#include <asm/unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>

#include <boost/asio.hpp>

struct Vec3 {
  float fX;
  float fY;
  float fZ;
};

enum class Dimension : int {
  NORMAL = 0,
  NETHER = 1,
  THE_END = 2,
};

static std::string StringFromDimension(Dimension d) {
  switch (d) {
  case Dimension::NORMAL:
    return "world";
  case Dimension::NETHER:
    return "world_nether";
  case Dimension::THE_END:
    return "world_the_end";
  default:
    return "";
  }
}

struct Location {
  Dimension fDimension;
  Vec3 fPos;
};

class Player {
public:
  struct Data {
    Data(Dimension d, Vec3 p, std::string const &n) : fDimension(d), fPos(p), fName(n) {}

    Dimension fDimension;
    Vec3 fPos;
    std::string fName;
  };

  explicit Player(void *address) : fAddress(address) {
  }

  std::optional<Data> data() const {
    if (!fPos || !fDimension || fName.empty()) {
      return std::nullopt;
    }
    return Data{*fDimension, *fPos, fName};
  }

  void setPos(Vec3 p) {
    fPos = p;
  }

  std::optional<Vec3> move(Vec3 delta) {
    if (!fPos) {
      return std::nullopt;
    }
    Vec3 n = *fPos;
    n.fX += delta.fX;
    n.fZ += delta.fZ;
    fPos = n;
    return n;
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

  void each(std::function<void(Player::Data const &data)> callback) const {
    for (auto it : fPlayers) {
      auto data = it.second->data();
      if (data) {
        callback(*data);
      }
    }
  }

  void forget(Player *player) {
    if (!player) {
      return;
    }
    fPlayers.erase(player->fAddress);
  }

  void report(std::ostream &s) const {
    s << "\"currentcount\":" << fPlayers.size() << ",";
    s << "\"players\":[";
    std::vector<Player::Data> players;
    each([&players](auto const &p) {
      players.push_back(p);
    });
    for (int i = 0; i < players.size(); i++) {
      auto const &player = players[i];
      s << "{";
      auto name = player.fName; //TODO: escape?
      s << "\"account\":\"" << name << "\",";
      s << "\"name\":\"" << name << "\",";
      s << "\"armor\":0,";
      s << "\"health\":20,";
      s << "\"sort\":" << i << ",";
      s << "\"type\":\"player\",";
      s << "\"world\":\"" << StringFromDimension(player.fDimension) << "\",";
      s << "\"x\":" << (int)player.fPos.fX << ",";
      s << "\"y\":" << (int)player.fPos.fY << ",";
      s << "\"z\":" << (int)player.fPos.fZ;
      s << "}";
      if (i < players.size() - 1) {
        s << ",";
      }
    }
    s << "],";
  }

private:
  std::map<void *, std::shared_ptr<Player>> fPlayers;
};

struct Weather {
  Weather() : fRain(false), fThunder(false) {}

  bool fRain;
  bool fThunder;

  void report(std::ostream &s) const {
    s << "\"hasStorm\":" << (fRain ? "true" : "false") << ",";
    s << "\"isThundering\":" << (fThunder ? "true" : "false") << ",";
  }
};

static time_t UnixTimestampMilli() {
  struct timeval now {};
  gettimeofday(&now, nullptr);
  return (now.tv_sec * 1000) + (now.tv_usec / 1000);
}

struct Level {
  PlayerRegistry fPlayers;
  Weather fWeather;
  int fTime;

  std::string report() const {
    std::ostringstream s;
    fPlayers.report(s);
    fWeather.report(s);
    s << "\"confighash\":0,";
    s << "\"servertime\":" << (fTime % 24000) << ",";
    s << "\"updates\":[]";
    return s.str();
  }

  static std::string FormatReport(std::string const &partial) {
    std::ostringstream s;
    s << "{";
    s << partial << ",";
    s << "\"timestamp\":" << UnixTimestampMilli();
    s << "}";
    return s.str();
  }
};

static Level sLevel;

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

using BreakpointCallback = std::function<void(pid_t, struct user_regs_struct)>;

struct Breakpoint {
  unsigned long fAddress;
  BreakpointCallback fCallback;
  long fOriginalInstruction;
};

namespace breakpoint {

namespace actor {

// Actor::setPos(Vec3 const&)
static void SetPos(pid_t pid, struct user_regs_struct regs) {
  auto player = sLevel.fPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  auto pos = ReadVec3(pid, (void *)regs.rsi);
  if (!pos) {
    return;
  }
  player->setPos(*pos);
}

} // namespace actor

namespace player {

// Player::move(Vec3 const&)
static void Move(pid_t pid, struct user_regs_struct regs) {
  auto address = (void *)regs.rdi;
  auto player = sLevel.fPlayers.unsafeGetByAddress(address);
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
static void SetName(pid_t pid, struct user_regs_struct regs) {
  auto address = (void *)regs.rdi;
  auto name = ReadString(pid, (void *)regs.rsi);
  if (!name) {
    return;
  }
  sLevel.fPlayers.getByName(*name, address);
}

} // namespace player

namespace server_player {

// ServerPlayer::changeDimension(AutomaticID<Dimension, int>, bool)
// ServerPlayer::changeDimensionWithCredits(AutomaticID<Dimension, int>)
static void ChangeDimension(pid_t pid, struct user_regs_struct regs) {
  auto player = sLevel.fPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  int dimension = (int)regs.rsi;
  player->setDimension(dimension);
}

// ServerPlayer::is2DPositionRelevant(AutomaticID<Dimension, int>, BlockPos const&)
static void Is2DPositionRelevant(pid_t pid, struct user_regs_struct regs) {
  auto player = sLevel.fPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  int dimension = (int)regs.rsi;
  player->setDimension(dimension);
}

// ServerPlayer::~ServerPlayer()
static void Destruct(pid_t pid, struct user_regs_struct regs) {
  auto player = sLevel.fPlayers.unsafeGetByAddress((void *)regs.rdi);
  if (!player) {
    return;
  }
  sLevel.fPlayers.forget(player);
}

} // namespace server_player

namespace level_event_coordinator {

// LevelEventCoordinator::sendLevelWeatherChanged(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, bool, bool)
static void SendLevelWeatherChanged(pid_t pid, struct user_regs_struct regs) {
  auto dimension = ReadString(pid, (void *)regs.rsi);
  bool rain = (bool)regs.rdx;
  bool thunder = (bool)regs.rcx;
  if (!dimension) {
    return;
  }
  if (*dimension == "Overworld" || *dimension == "overworld") {
    sLevel.fWeather.fRain = rain;
    sLevel.fWeather.fThunder = thunder;
  }
}

} // namespace level_event_coordinator

namespace set_time_pakcet {

// SetTimePacket::SetTimePacket(int)
static void Construct(pid_t pid, struct user_regs_struct regs) {
  int t = (int)regs.rsi;
  sLevel.fTime = t;
}

} // namespace set_time_pakcet

static void Debug(pid_t pid, struct user_regs_struct regs) {
  // rdi rsi rdx rcx r8 r9
  using namespace std;
  cout << __FUNCTION__ << endl;
}

} // namespace breakpoint

static void Report(std::string s) {
  std::cout << Level::FormatReport(s) << std::endl;
}

int main(int argc, char *argv[]) {
  pid_t pid = atoi(argv[1]);

  // 1.16.220.02
  Breakpoint breakpoints[] = {
      {.fAddress = 0x0000000001f9fbd0, .fCallback = breakpoint::actor::SetPos},
      {.fAddress = 0x0000000001b172b0, .fCallback = breakpoint::player::Move},
      {.fAddress = 0x0000000001b14270, .fCallback = breakpoint::player::SetName},
      {.fAddress = 0x00000000016ac180, .fCallback = breakpoint::server_player::ChangeDimension},
      {.fAddress = 0x00000000016ac290, .fCallback = breakpoint::server_player::ChangeDimension},
      {.fAddress = 0x00000000016ac970, .fCallback = breakpoint::server_player::Is2DPositionRelevant},
      {.fAddress = 0x00000000016a46c0, .fCallback = breakpoint::server_player::Destruct},
      {.fAddress = 0x00000000016a4530, .fCallback = breakpoint::server_player::Destruct},
      {.fAddress = 0x00000000022cb030, .fCallback = breakpoint::level_event_coordinator::SendLevelWeatherChanged},
      {.fAddress = 0x00000000011e0b00, .fCallback = breakpoint::set_time_pakcet::Construct},
  };
  size_t const kNumBreakpoints = sizeof(breakpoints) / sizeof(Breakpoint);

  std::string last = sLevel.report();
  boost::asio::io_context ctx;
  boost::asio::io_context::work work(ctx);
  std::thread thread([&ctx]() { ctx.run(); });
  ctx.post(std::bind(Report, last));

  AttachAllThread(pid);

  syscall(__NR_tkill, pid, SIGSTOP);
  int s;
  waitpid(pid, &s, 0);

  long const kInt3Opcode = 0x000000cc;
  for (size_t i = 0; i < kNumBreakpoints; i++) {
    unsigned long address = breakpoints[i].fAddress;
    long original = ptrace(PTRACE_PEEKTEXT, pid, address, 0);
    breakpoints[i].fOriginalInstruction = original;
    ptrace(PTRACE_POKETEXT, pid, address, ((original & 0xFFFFFFFFFFFFFF00) | kInt3Opcode));
  }

  ptrace(PTRACE_CONT, pid, 0, 0);

  int status;
  while (true) {
    int tid = waitpid(-1, &status, __WALL);

    if (WIFEXITED(status)) {
      break;
    }

    if (!WIFSTOPPED(status)) {
      break;
    }

    int signum = WSTOPSIG(status);
    if (signum == SIGTRAP) {
      struct user_regs_struct regs;
      ptrace(PTRACE_GETREGS, tid, 0, &regs);

      bool found = false;
      Breakpoint target;
      for (size_t i = 0; i < kNumBreakpoints; i++) {
        if (breakpoints[i].fAddress + 1 == regs.rip) {
          target = breakpoints[i];
          found = true;
          break;
        }
      }

      if (found) {
        target.fCallback(pid, regs);

        auto report = sLevel.report();
        if (last != report) {
          ctx.post(std::bind(Report, report));
          last = report;
        }

        ptrace(PTRACE_POKETEXT, tid, target.fAddress, target.fOriginalInstruction);
        regs.rip--;
        ptrace(PTRACE_SETREGS, tid, 0, &regs);

        ptrace(PTRACE_SINGLESTEP, tid, 0, 0);
        waitpid(tid, &status, __WALL);
        ptrace(PTRACE_POKETEXT, tid, target.fAddress, ((target.fOriginalInstruction & 0xFFFFFFFFFFFFFF00) | kInt3Opcode));
      }

      ptrace(PTRACE_CONT, tid, 0, 0);
    } else if (signum == 19 || signum == 21) {
      ptrace(PTRACE_CONT, tid, 0, 0);
    } else {
      ptrace(PTRACE_CONT, tid, 0, signum);
    }
  }

  ctx.stop();
  thread.join();
}
