#pragma once
constexpr int c_syncInterval{5 /* seconds */};
constexpr unsigned c_maxMsgSize{100 /* characters */};
constexpr unsigned c_maxUserName{15 /* characters */};
constexpr unsigned c_minUserName{3 /* characters */};
constexpr unsigned c_turnLength{10 /* seconds */};
constexpr unsigned c_heartbeatTimeout{3 /* seconds */};

typedef std::string EmuID_t;
