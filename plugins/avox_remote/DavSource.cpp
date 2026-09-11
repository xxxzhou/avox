#include "DavSource.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

namespace avox {

namespace {

// PROPFIND 请求体: 只要列表所需的最小属性集
const char* kPropfindBody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop>"
    "<d:displayname/><d:resourcetype/><d:getcontentlength/>"
    "</d:prop></d:propfind>";

std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

std::string trimCopy(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) {
    return "";
  }
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percentDecode(const std::string& s) {
  if (s.find('%') == std::string::npos) {
    return s;
  }
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int hi = hexVal(s[i + 1]);
      int lo = hexVal(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back((char)(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string percentEncode(const std::string& s, const char* keep) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (std::isalnum(c) || std::strchr("-._~", c) != nullptr ||
        (keep != nullptr && std::strchr(keep, c) != nullptr)) {
      out.push_back((char)c);
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 15]);
    }
  }
  return out;
}

// token(解码路径) → 逐段编码的请求路径 (保留 '/', 绝对路径兜底)
std::string encodePath(const std::string& token) {
  std::string out;
  size_t start = 0;
  while (start <= token.size()) {
    size_t slash = token.find('/', start);
    std::string seg = token.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    out += percentEncode(seg, nullptr);
    if (slash == std::string::npos) {
      break;
    }
    out += '/';
    start = slash + 1;
  }
  if (out.empty() || out[0] != '/') {
    out = "/" + out;
  }
  return out;
}

std::string xmlDecode(const std::string& s) {
  if (s.find('&') == std::string::npos) {
    return s;
  }
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out.push_back(s[i]);
      continue;
    }
    size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) {
      out.push_back(s[i]);
      continue;
    }
    std::string ent = s.substr(i + 1, semi - i - 1);
    if (ent == "amp") {
      out.push_back('&');
    } else if (ent == "lt") {
      out.push_back('<');
    } else if (ent == "gt") {
      out.push_back('>');
    } else if (ent == "quot") {
      out.push_back('"');
    } else if (ent == "apos") {
      out.push_back('\'');
    } else if (!ent.empty() && ent[0] == '#') {
      // 数字引用: &#NN; / &#xHH; (ASCII 范围, 多字节 UTF-8 一般不经实体)
      int v = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                  ? (int)strtol(ent.c_str() + 2, nullptr, 16)
                  : (int)strtol(ent.c_str() + 1, nullptr, 10);
      if (v > 0 && v < 256) {
        out.push_back((char)v);
      }
    } else {
      out += "&" + ent + ";";
    }
    i = semi;
  }
  return out;
}

// 在 low(原文小写副本) 中从 pos 起找局部名为 localName 的下一个标签'<'
// closeOnly: 只找闭合形态 </...>; 跳过 <?与<!--; 命名空间前缀忽略
size_t findLocalTag(const std::string& low, size_t pos,
                    const std::string& localName, bool closeOnly) {
  while (true) {
    size_t lt = low.find('<', pos);
    if (lt == std::string::npos) {
      return std::string::npos;
    }
    size_t i = lt + 1;
    bool closing = false;
    if (i < low.size() && low[i] == '/') {
      closing = true;
      ++i;
    }
    if (i >= low.size() || low[i] == '?' || low[i] == '!') {
      pos = lt + 1;
      continue;
    }
    size_t nameStart = i;
    while (i < low.size() && !std::isspace((unsigned char)low[i]) &&
           low[i] != '/' && low[i] != '>') {
      ++i;
    }
    std::string name = low.substr(nameStart, i - nameStart);
    size_t colon = name.rfind(':');
    if (colon != std::string::npos) {
      name = name.substr(colon + 1);
    }
    if (name == localName && closing == closeOnly) {
      return lt;
    }
    pos = lt + 1;
  }
}

// [begin,end) 内局部名为 localName 的元素文本 (自闭合/未闭合返回空)
std::string tagTextRange(const std::string& xml, const std::string& low,
                         size_t begin, size_t end,
                         const std::string& localName) {
  size_t lt = findLocalTag(low, begin, localName, false);
  if (lt == std::string::npos || lt >= end) {
    return "";
  }
  size_t gt = low.find('>', lt);
  if (gt == std::string::npos || gt >= end) {
    return "";
  }
  if (gt > lt && low[gt - 1] == '/') {
    return "";  // 自闭合
  }
  size_t close = findLocalTag(low, gt, localName, true);
  size_t textEnd = (close == std::string::npos || close > end) ? end : close;
  return xmlDecode(trimCopy(xml.substr(gt + 1, textEnd - (gt + 1))));
}

// [begin,end) 内是否出现局部名为 localName 的标签 (如 resourcetype 下的 collection)
bool blockHasTag(const std::string& low, size_t begin, size_t end,
                 const std::string& localName) {
  return findLocalTag(low, begin, localName, false) != std::string::npos &&
         findLocalTag(low, begin, localName, false) < end;
}

bool isMediaPath(const std::string& path) {
  static const char* kExts[] = {".mp4", ".mkv", ".ts",  ".flv", ".webm",
                                ".avi", ".mov", ".m4v", ".mpg", ".mpeg",
                                ".wmv", ".3gp"};
  size_t dot = path.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = toLowerCopy(path.substr(dot));
  for (auto* e : kExts) {
    if (ext == e) {
      return true;
    }
  }
  return false;
}

}  // namespace

DavSource::DavSource() {}

DavSource::~DavSource() { close(); }

void DavSource::setOb(IRemoteSourceOb* ob) { obAt.store(ob); }

uint32_t DavSource::getCaps() {
  // 纯 WebDAV 协议无服务端搜索/缩略图/播放状态 (Alist 原生 API 驱动后续再加)
  return 0;
}

RemoteAuthKind DavSource::getAuthKind() { return RemoteAuthKind::userPass; }

void DavSource::setParam(const char* key, const char* value) {
  if (key == nullptr) {
    return;
  }
  // 自签名证书场景关校验 (内网 NAS 常见)
  if (std::strcmp(key, "verifyTls") == 0) {
    verifyTls = value != nullptr && std::strcmp(value, "false") == 0;
  }
}

bool DavSource::parseUrl(const std::string& url, UrlParts* out) {
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) {
    return false;
  }
  out->scheme = toLowerCopy(url.substr(0, schemeEnd));
  if (out->scheme != "http" && out->scheme != "https") {
    return false;
  }
  size_t rest = schemeEnd + 3;
  size_t pathStart = url.find('/', rest);
  std::string authority =
      url.substr(rest, pathStart == std::string::npos ? std::string::npos
                                                      : pathStart - rest);
  std::string path =
      pathStart == std::string::npos ? "/" : url.substr(pathStart);
  // userinfo(取 host 前最后一个 '@'): user[:pass], 可为百分号编码
  size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    std::string ui = authority.substr(0, at);
    authority = authority.substr(at + 1);
    size_t colon = ui.find(':');
    if (colon == std::string::npos) {
      out->user = percentDecode(ui);
    } else {
      out->user = percentDecode(ui.substr(0, colon));
      out->pass = percentDecode(ui.substr(colon + 1));
    }
  }
  // host:port (IPv6 简单支持 [..]:port)
  if (!authority.empty() && authority[0] == '[') {
    size_t rb = authority.find(']');
    if (rb == std::string::npos) {
      return false;
    }
    out->host = authority.substr(1, rb - 1);
    if (rb + 1 < authority.size() && authority[rb + 1] == ':') {
      out->port = std::atoi(authority.c_str() + rb + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos &&
        authority.find(':') == colon) {  // 单冒号才当端口, 防裸 IPv6
      out->host = authority.substr(0, colon);
      out->port = std::atoi(authority.c_str() + colon + 1);
    } else {
      out->host = authority;
    }
  }
  if (out->host.empty()) {
    return false;
  }
  if (out->port == 0) {
    out->port = out->scheme == "https" ? 443 : 80;
  }
  // root token: 解码 + 补尾 '/'
  out->rootToken = percentDecode(path);
  if (out->rootToken.empty()) {
    out->rootToken = "/";
  }
  if (out->rootToken.back() != '/') {
    out->rootToken += '/';
  }
  return true;
}

bool DavSource::open(const char* url, const char* user, const char* pass,
                     const char* token, int32_t timeoutMs) {
  (void)token;  // dav 无 token 形态鉴权 (Basic 走 user/pass)
  if (running()) {
    return false;
  }
  if (url == nullptr || *url == '\0') {
    return false;
  }
  auto p = std::make_unique<UrlParts>();
  if (!parseUrl(url, p.get())) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "unsupported url(expect http/https WebDAV entry)";
    return false;
  }
  if (user != nullptr && *user != '\0') {
    p->user = user;
    p->pass = pass != nullptr ? pass : "";
  }
  opTimeoutMs = timeoutMs > 0 ? timeoutMs : 10000;
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    batch.clear();
    sessionName = "";
    lastError = "";
    resolvedBuf = "";
  }
  parts = std::move(p);  // startTask 前写完, 工作线程读无竞态
  openedFlag.store(false);
  abortFlag.store(false);
  op = Op::open;
  taskName = "dav open";
  startTask();
  return true;
}

void DavSource::close() {
  // httplib 请求不可中途打断: 当前请求返回后工作线程见 abortFlag 丢弃结果退出
  abortFlag.store(true);
  openedFlag.store(false);
  if (running()) {
    stopTask();
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  batch.clear();
  sessionName = "";
  lastError = "";
  resolvedBuf = "";
}

bool DavSource::opened() { return openedFlag.load(); }

bool DavSource::list(const char* nodeToken, int32_t timeoutMs) {
  if (running() || !openedFlag.load()) {
    return false;
  }
  listToken = nodeToken != nullptr ? nodeToken : "";
  opTimeoutMs = timeoutMs > 0 ? timeoutMs : opTimeoutMs;
  abortFlag.store(false);
  op = Op::list;
  taskName = "dav list";
  startTask();
  return true;
}

void DavSource::stopList() {
  abortFlag.store(true);
  if (running()) {
    stopTask();
  }
}

bool DavSource::propfind(const std::string& reqPath, int depth,
                         std::string* body, int32_t* statusCode) {
  *statusCode = 0;
  const UrlParts& p = *parts;
  // 本版 httplib 无 (scheme,host,port) 三参构造, 用 scheme://host:port 单串
  std::string hostPart =
      p.host.find(':') != std::string::npos ? "[" + p.host + "]" : p.host;
  httplib::Client cli(p.scheme + "://" + hostPart + ":" + std::to_string(p.port));
  auto secs = std::max<int32_t>(1, (opTimeoutMs + 999) / 1000);
  cli.set_connection_timeout(secs, 0);
  cli.set_read_timeout(secs, 0);
  cli.set_write_timeout(secs, 0);
  if (!p.user.empty()) {
    cli.set_basic_auth(p.user.c_str(), p.pass.c_str());
  }
  if (!verifyTls) {
    cli.enable_server_certificate_verification(false);
  }
  httplib::Request req;
  req.method = "PROPFIND";
  req.path = reqPath;
  req.headers.emplace("Depth", depth > 0 ? "1" : "0");
  req.headers.emplace("Content-Type", "application/xml");
  req.body = kPropfindBody;
  httplib::Response res;
  httplib::Error err = httplib::Error::Success;
  if (!cli.send(req, res, err)) {
    // getter 并发读 lastError, 写入走结果锁
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = std::string("dav request failed: ") + httplib::to_string(err);
    *statusCode = -1;
    return false;
  }
  *statusCode = (int32_t)res.status;
  *body = std::move(res.body);
  return true;
}

void DavSource::runOpen() {
  // PROPFIND Depth0 验会话, 顺带取根 displayname
  std::string body;
  int32_t status = 0;
  bool ok = propfind(requestPath(parts->rootToken), 0, &body, &status);
  int32_t code = (int32_t)RemoteCode::ok;
  if (ok && (status == 207 || status == 200)) {
    auto items = parseMultistatus(body);
    std::lock_guard<std::mutex> lk(resultMutex);
    sessionName = baseName(parts->rootToken);
    for (const auto& it : items) {
      if (!it.displayName.empty()) {
        sessionName = it.displayName;
        break;
      }
    }
    if (sessionName.empty() || sessionName == "/") {
      sessionName = parts->host;
    }
    lastError = "";
    openedFlag.store(true);
  } else if (!ok) {
    code = (int32_t)RemoteCode::net;
  } else if (status == 401 || status == 403) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "auth failed(http " + std::to_string(status) + ")";
    code = (int32_t)RemoteCode::authFailed;
  } else {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "http " + std::to_string(status);
    code = (int32_t)RemoteCode::other;
  }
  // 上层已 close: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onOpenResult(code);
  }
}

void DavSource::runList() {
  std::string dir = listToken.empty() ? parts->rootToken : listToken;
  if (!isDirToken(dir)) {
    dir += "/";  // 容错: 目录 token 必须带尾'/'
  }
  std::string body;
  int32_t status = 0;
  bool ok = propfind(requestPath(dir), 1, &body, &status);
  int32_t code = (int32_t)RemoteCode::ok;
  if (ok && (status == 207 || status == 200)) {
    auto items = parseMultistatus(body);
    std::lock_guard<std::mutex> lk(resultMutex);
    if (!fillBatch(items, dir)) {
      lastError = "no entries under " + dir;
      code = (int32_t)RemoteCode::notFound;
    }
  } else if (!ok) {
    code = (int32_t)RemoteCode::net;
  } else if (status == 401 || status == 403) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "auth failed(http " + std::to_string(status) + ")";
    code = (int32_t)RemoteCode::authFailed;
  } else {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "http " + std::to_string(status);
    code = status == 404 ? (int32_t)RemoteCode::notFound
                         : (int32_t)RemoteCode::other;
  }
  // 上层已 stopList/close: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onListResult(code);
  }
}

void DavSource::onRunTask() {
  if (op == Op::open) {
    runOpen();
  } else {
    runList();
  }
}

std::vector<DavSource::DavItem> DavSource::parseMultistatus(
    const std::string& xml) {
  std::vector<DavItem> out;
  std::string low = toLowerCopy(xml);
  size_t pos = 0;
  while (true) {
    size_t open = findLocalTag(low, pos, "response", false);
    if (open == std::string::npos) {
      break;
    }
    size_t close = findLocalTag(low, open, "response", true);
    if (close == std::string::npos) {
      break;
    }
    DavItem it;
    it.href = percentDecode(tagTextRange(xml, low, open, close, "href"));
    it.displayName = tagTextRange(xml, low, open, close, "displayname");
    std::string len = tagTextRange(xml, low, open, close, "getcontentlength");
    it.size = len.empty() ? 0 : (uint64_t)std::strtoull(len.c_str(), nullptr, 10);
    // 目录判定: resourcetype 内含 collection 标签 (href 尾'/'兜底)
    size_t rt = findLocalTag(low, open, "resourcetype", false);
    it.dir = rt != std::string::npos && rt < close &&
             blockHasTag(low, rt, close, "collection");
    if (!it.href.empty()) {
      out.push_back(std::move(it));
    }
    pos = close;
  }
  return out;
}

std::string DavSource::dirOf(const std::string& filePath) {
  size_t pos = filePath.find_last_of('/');
  return pos == std::string::npos ? "/" : filePath.substr(0, pos + 1);
}

std::string DavSource::baseName(const std::string& token) {
  std::string noSlash = token;
  if (noSlash.size() > 1 && noSlash.back() == '/') {
    noSlash.pop_back();
  }
  size_t pos = noSlash.find_last_of('/');
  return pos == std::string::npos ? noSlash : noSlash.substr(pos + 1);
}

std::string DavSource::parentToken(const std::string& dirToken) {
  std::string dir = dirToken.substr(0, dirToken.size() - 1);
  size_t pos = dir.find_last_of('/');
  return pos == std::string::npos ? "" : dir.substr(0, pos + 1);
}

bool DavSource::isDirToken(const std::string& token) {
  return !token.empty() && token.back() == '/';
}

bool DavSource::fillBatch(const std::vector<DavItem>& items,
                          const std::string& reqDir) {
  batch.clear();
  std::string reqNorm = reqDir;
  if (reqNorm.size() > 1 && reqNorm.back() == '/') {
    reqNorm.pop_back();  // 自身比对口径: 去尾'/'
  }
  for (const auto& it : items) {
    // href 归一成服务器绝对路径 (绝对 URL 剥 scheme+host, 去掉 query)
    std::string href = it.href;
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
      size_t slash = href.find('/', href.find("//") + 2);
      href = slash == std::string::npos ? "/" : href.substr(slash);
    }
    size_t query = href.find('?');
    if (query != std::string::npos) {
      href = href.substr(0, query);
    }
    if (href.empty() || href[0] != '/') {
      continue;
    }
    bool dir = it.dir || (!href.empty() && href.back() == '/');
    std::string norm = href;
    if (dir && norm.size() > 1 && norm.back() == '/') {
      norm.pop_back();
    }
    // 排除目录自身 (Depth1 的第一个 response)
    if (norm == reqNorm) {
      continue;
    }
    Entry e;
    e.type = dir ? RemoteEntryType::dir
                 : (isMediaPath(norm) ? RemoteEntryType::media
                                      : RemoteEntryType::file);
    e.name = it.displayName.empty() ? baseName(norm) : it.displayName;
    e.token = dir ? norm + "/" : norm;
    e.size = dir ? 0 : it.size;
    batch.push_back(std::move(e));
  }
  // 目录在前, 同类名称升序
  std::stable_sort(batch.begin(), batch.end(),
                   [](const Entry& a, const Entry& b) {
                     if (a.type != b.type) {
                       return a.type == RemoteEntryType::dir;
                     }
                     return a.name < b.name;
                   });
  return !batch.empty();
}

std::string DavSource::entryUrl(const std::string& token) const {
  const UrlParts& p = *parts;
  std::string auth;
  if (!p.user.empty()) {
    auth = percentEncode(p.user, nullptr) + ":" +
           percentEncode(p.pass, nullptr) + "@";
  }
  std::string port;
  bool defaultPort =
      (p.scheme == "https" && p.port == 443) || (p.scheme == "http" && p.port == 80);
  if (!defaultPort) {
    port = ":" + std::to_string(p.port);
  }
  return p.scheme + "://" + auth + p.host + port + encodePath(token);
}

std::string DavSource::requestPath(const std::string& token) const {
  return encodePath(token);
}

int32_t DavSource::getEntryCount() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (int32_t)batch.size();
}

RemoteEntryType DavSource::getEntryType(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].type
                                               : RemoteEntryType::other;
}

const char* DavSource::getEntryName(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].name.c_str() : "";
}

uint64_t DavSource::getEntrySize(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].size : 0;
}

const char* DavSource::getEntryToken(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].token.c_str() : "";
}

const char* DavSource::getSessionField(const char* key) {
  if (key == nullptr) {
    return "";
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  if (std::strcmp(key, "name") == 0) {
    return sessionName.c_str();
  }
  if (parts && std::strcmp(key, "host") == 0) {
    return parts->host.c_str();
  }
  return "";
}

const char* DavSource::resolve(int32_t entryIndex, IOption* option) {
  (void)option;  // dav 播放无需私有 option 键 (鉴权已嵌 userinfo)
  std::lock_guard<std::mutex> lk(resultMutex);
  if (entryIndex < 0 || entryIndex >= (int32_t)batch.size()) {
    lastError = "entry index out of range";
    return nullptr;
  }
  const Entry& e = batch[entryIndex];
  if (e.type == RemoteEntryType::dir) {
    lastError = "entry is a directory(先 list 下钻): " + e.token;
    return nullptr;
  }
  resolvedBuf = entryUrl(e.token);
  return resolvedBuf.c_str();
}

const char* DavSource::getLastError() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return lastError.c_str();
}

}
