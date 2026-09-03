#include "TeamValidation.hpp"

#include <cctype>

namespace avox {

std::string teamRequiredText(const std::string& value, const char* field,
                             size_t maxLength) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  while (end > begin
         && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  const std::string text = value.substr(begin, end - begin);
  if (text.empty()) {
    throw TeamError(std::string(field) + " must be non-empty",
                    TEAM_INVALID_ARGUMENT);
  }
  if (text.size() > maxLength) {
    throw TeamError(std::string(field) + " exceeds "
                        + std::to_string(maxLength) + " characters",
                    TEAM_INVALID_ARGUMENT);
  }
  return text;
}

void checkTeamMemberName(const std::string& name) {
  const auto fail = [&name](const char* why) {
    throw TeamError("invalid teammate name \"" + name + "\": " + why,
                    TEAM_INVALID_MEMBER_NAME);
  };
  if (name.empty()) fail("must be non-empty");
  if (name.size() > 64) fail("must be at most 64 characters");
  if (name == "lead") fail("\"lead\" is reserved for the Team Lead");
  bool expectSegmentStart = true;
  for (const char c : name) {
    const bool segmentChar = std::isdigit(static_cast<unsigned char>(c)) != 0
                             || std::islower(static_cast<unsigned char>(c)) != 0;
    if (c == '-') {
      // '-' 只能出现在段间, 且不能收尾 —— 即不允许 "--" / "a-" / "-a"。
      if (expectSegmentStart) fail("must be lower-kebab-case ([a-z0-9]+(-[a-z0-9]+)*)");
      expectSegmentStart = true;
    } else if (segmentChar) {
      expectSegmentStart = false;
    } else {
      fail("must be lower-kebab-case ([a-z0-9]+(-[a-z0-9]+)*)");
    }
  }
  if (expectSegmentStart) {
    fail("must be lower-kebab-case ([a-z0-9]+(-[a-z0-9]+)*)");
  }
}

std::string teamWriteScope(const std::string& value) {
  std::string normalized = value;
  for (char& c : normalized) {
    if (c == '\\') c = '/';
  }
  if (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/') {
    normalized.erase(0, 2);
  }
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  const auto reject = [&value]() {
    throw TeamError("invalid workspace-relative write scope \"" + value + "\"",
                    TEAM_INVALID_WRITE_SCOPE);
  };
  if (normalized.empty() || normalized.front() == '/') reject();
  // 盘符前缀 (a: / A:)。
  if (normalized.size() >= 2 && normalized[1] == ':') reject();
  size_t segmentBegin = 0;
  for (size_t i = 0; i <= normalized.size(); ++i) {
    if (i != normalized.size() && normalized[i] != '/') continue;
    const std::string segment =
        normalized.substr(segmentBegin, i - segmentBegin);
    segmentBegin = i + 1;
    if (segment.empty() || segment == "." || segment == "..") reject();
  }
  return normalized;
}

}
