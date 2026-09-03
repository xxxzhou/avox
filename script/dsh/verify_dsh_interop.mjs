#!/usr/bin/env node
// avox ⇄ dsh 会话日志互通验证 (dsh 侧装载)。
//
// 用 dsh 自己的装载管线读 avox 产的会话日志: 全部通过 = avox 写出的日志 dsh 读得懂、
// 能在其上 resume。dsh 仓零改动 —— 本脚本只消费其构建产物 (packages/*/lib)。
//
// 三段校验:
//   A. 逐行 wire 解析 (dsh-llm-replay parseSessionLog: 打包行展开, 坏行响亮报错)
//   B. 完整持久化装载 (cordis Context + SessionStore + JsonlSessionPersistence
//      compression:'none' → load(id): 目录布局互证 + 头行校验 + 严格解码)
//   C. surface 折叠 (foldSurface: replace 区间 / 来源引用 / tool-result 重写规则)
//
// 用法:
//   node script/dsh/verify_dsh_interop.mjs [目标] [--dsh <deepseek-harness 仓路径>]
//
// 目标 (三选一, 缺省 = 扫描默认会话根取最新一份):
//   <某路径>/session.jsonl   日志文件完整路径 (根取其上三级目录)
//   <会话id>                 在会话根下按 id 扫描装载
//   <目录>                   会话根目录 (取其下最新一份)
//
// 会话根解析顺序: 目标携带的路径 > $DSH_HOME/sessions > ~/.dsh/sessions。
// DSH 仓路径解析顺序: --dsh > $DSH_REPO > <avox仓>/../deepseek-harness。
//
// 退出码: 0 = 通过; 1 = 拒读或校验失败 (附 dsh 侧报错原文, 那就是互通断点)。

import { cpSync, existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync } from 'node:fs'
import { createRequire } from 'node:module'
import { basename, dirname, join, resolve } from 'node:path'
import { homedir, tmpdir } from 'node:os'
import { fileURLToPath, pathToFileURL } from 'node:url'

const scriptDir = dirname(fileURLToPath(import.meta.url))
// 脚本位于 script/dsh/, 上移两级才是 avox 仓根。
const avoxRoot = resolve(scriptDir, '..', '..')

// ---------- 参数 ----------

const args = process.argv.slice(2)
const dshFlag = args.indexOf('--dsh')
const dshRoot = dshFlag >= 0 ? resolve(args[dshFlag + 1]) : null
args.splice(dshFlag >= 0 ? dshFlag : 0, dshFlag >= 0 ? 2 : 0)
const target = args[0] ?? null

function fail(message) {
  console.error(`FAIL: ${message}`)
  process.exit(1)
}

function resolveDshRepo() {
  const candidates = [
    dshRoot,
    process.env.DSH_REPO,
    join(dirname(avoxRoot), 'deepseek-harness'),
  ].filter(Boolean)
  for (const candidate of candidates) {
    if (existsSync(join(candidate, 'packages', 'core', 'session', 'lib', 'index.js'))) {
      return candidate
    }
  }
  fail(`找不到 deepseek-harness 构建产物 (需要 packages/core/session/lib/index.js)。`
    + ` 用 --dsh <路径> 或 $DSH_REPO 指定, 且先在仓根跑一次构建。候选: ${candidates.join(', ')}`)
}

function sessionsRootDefault() {
  const home = process.env.DSH_HOME
  const base = home && home.trim() ? home : join(homedir(), '.dsh')
  return join(base, 'sessions')
}

// 找出会话根下最新一份 session.jsonl (两层目录: <projectKey>/<encodeSegment(id)>/)。
function newestLogUnder(root) {
  let newest = null
  for (const project of readdirSync(root, { withFileTypes: true })) {
    if (!project.isDirectory()) continue
    const projectDir = join(root, project.name)
    for (const segment of readdirSync(projectDir, { withFileTypes: true })) {
      if (!segment.isDirectory()) continue
      const log = join(projectDir, segment.name, 'session.jsonl')
      if (!existsSync(log)) continue
      const mtime = statSync(log).mtimeMs
      if (newest === null || mtime > newest.mtime) newest = { log, mtime }
    }
  }
  return newest?.log ?? null
}

// ---------- 目标解析 ----------

function resolveTarget() {
  if (target === null) {
    const root = sessionsRootDefault()
    if (!existsSync(root)) fail(`默认会话根不存在: ${root} (先跑一轮 avox agent 或传目标路径)`)
    const log = newestLogUnder(root)
    if (log === null) fail(`会话根 ${root} 下没有任何 session.jsonl`)
    return { log, root }
  }
  const absolute = resolve(target)
  if (!existsSync(absolute)) fail(`目标不存在: ${absolute}`)
  const stat = statSync(absolute)
  if (stat.isFile()) {
    // 日志文件: 根 = 上三级 (root/projectKey/segment/session.jsonl)。
    if (basename(absolute) !== 'session.jsonl') {
      fail(`文件名不是 session.jsonl: ${absolute} (dsh 布局下的日志文件固定叫这个名)`)
    }
    return { log: absolute, root: dirname(dirname(dirname(absolute))) }
  }
  // 目录: 会话根或会话目录, 取其下最新一份。
  const log = basename(absolute) === 'session.jsonl'
    ? absolute
    : (existsSync(join(absolute, 'session.jsonl'))
        ? join(absolute, 'session.jsonl')
        : newestLogUnder(absolute))
  if (log === null) fail(`目录下没有 session.jsonl: ${absolute}`)
  const root = basename(dirname(log)) !== 'session.jsonl' && basename(log) === 'session.jsonl'
    ? dirname(dirname(dirname(log)))
    : absolute
  return { log, root }
}

// ---------- dsh 侧库装载 ----------

async function loadDshLibraries(dshRepo) {
  // 三个包都是 ESM 且互不依赖成环, 直接按 lib 绝对路径 import; 其内部 bare import
  // 从各自包的 node_modules 解析 (pnpm 布局)。cordis 入口用 require.resolve 从
  // dsh-session 的依赖链里探, 免得猜包的 exports 形状。
  const sessionEntry = pathToFileURL(
    join(dshRepo, 'packages', 'core', 'session', 'lib', 'index.js')).href
  const session = await import(sessionEntry)
  const jsonlEntry = pathToFileURL(
    join(dshRepo, 'packages', 'session', 'session-persistence-jsonl', 'lib', 'index.js')).href
  const jsonl = await import(jsonlEntry)
  const replayEntry = pathToFileURL(
    join(dshRepo, 'packages', 'test-support', 'llm-replay', 'lib', 'index.js')).href
  const replay = await import(replayEntry)
  const sessionRequire = createRequire(
    join(dshRepo, 'packages', 'core', 'session', 'lib', 'index.js'))
  const cordisEntry = pathToFileURL(sessionRequire.resolve('@deepseek-ai/cordis')).href
  const cordis = await import(cordisEntry)
  return {
    SessionStore: session.SessionStore ?? session.default,
    SessionId: session.SessionId,
    foldSurface: session.foldSurface,
    JsonlSessionPersistence: jsonl.default ?? jsonl.JsonlSessionPersistence,
    parseSessionLog: replay.parseSessionLog,
    Context: cordis.Context ?? cordis.default?.Context,
  }
}

// ---------- 三段校验 ----------

function readHeaderOf(log) {
  const text = readFileSync(log)
  if (text.length >= 4 && text[0] === 0x28 && text[1] === 0xb5 && text[2] === 0x2f
      && text[3] === 0xfd) {
    fail(`${log} 是 zstd 压缩档: avox 全明文, 请核对这份日志的来源 `
      + `(dsh 侧需配置 compression:'none' 重录)`)
  }
  const firstLine = text.toString('utf8').split('\n').find(line => line.trim().length > 0)
  if (firstLine === undefined) fail(`${log} 是空文件`)
  return JSON.parse(firstLine)
}

async function main() {
  const dshRepo = resolveDshRepo()
  const { log, root } = resolveTarget()
  const libs = await loadDshLibraries(dshRepo)
  console.log(`日志: ${log}`)
  console.log(`根:  ${root}`)

  // A. wire 解析 (llm-replay: 打包行在此展开)。
  const events = libs.parseSessionLog(readFileSync(log, 'utf8'))
  events.forEach((event, index) => {
    if (event.seq !== index) fail(`wire 解析后 seq 不连续: 第 ${index} 条是 ${event.seq}`)
  })
  console.log(`A. wire 解析 ok: ${events.length} 条事件 (含打包行展开)`)

  // B. 完整装载 (布局互证 + 头行校验 + 严格解码)。avox 全明文, compression 恒 'none'。
  //
  // 会话根里若混有 dsh 默认压缩的 .jsonl.zstd 兄弟会话, dsh 的「同 root 混编码即硬
  // 错误」规则会拒掉整个 root 的 'none' 挂载。这里把目标日志按原目录名复制进临时
  // root 再装载: 隔离兄弟工件, 而 assertStoredIdentity 仍然拿头行 cwd/id 对照目录名
  // —— avox 的 projectKey/encodeSegment 编码对不对, 由 dsh 自己来判。
  const header = readHeaderOf(log)
  const segmentDir = basename(dirname(log))
  const projectDirName = basename(dirname(dirname(log)))
  if (!/^--[A-Za-z0-9._~-]*--$/.test(projectDirName)) {
    fail(`日志不在 dsh 布局下 (上级目录名不是 projectKey 形状): ${projectDirName}`)
  }
  const tmpRoot = mkdtempSync(join(tmpdir(), 'avox-dsh-verify-'))
  const ctx = new libs.Context()
  let loaded
  try {
    cpSync(log, join(tmpRoot, projectDirName, segmentDir, 'session.jsonl'),
      { recursive: true })
    await ctx.plugin(libs.SessionStore)
    await ctx.plugin(libs.JsonlSessionPersistence, { root: tmpRoot, compression: 'none' })
    loaded = await ctx.sessionPersistence.load(libs.SessionId(header.id))
  } finally {
    await ctx.fiber.dispose()
    rmSync(tmpRoot, { recursive: true, force: true })
  }
  if (loaded.meta.id !== header.id) fail(`装载后的 id 与头行不一致`)
  if (loaded.events.length !== events.length) {
    fail(`wire 解析 ${events.length} 条 vs 持久化装载 ${loaded.events.length} 条`)
  }
  console.log(`B. 持久化装载 ok: id=${loaded.meta.id}, cwd=${loaded.meta.cwd ?? '(无)'}, `
    + `${loaded.events.length} 条事件`)

  // C. surface 折叠 (replace 区间 / 来源引用 / tool-result 重写规则)。
  const folded = libs.foldSurface(loaded.events)
  const kinds = { 'user/message': 0, 'assistant/message': 0, 'tool/result': 0 }
  for (const seq of folded.nodes) {
    const type = loaded.events[seq].type
    if (type in kinds) kinds[type] += 1
  }
  console.log(`C. surface 折叠 ok: ${folded.nodes.length} 个可见节点 `
    + `(user ${kinds['user/message']}, assistant ${kinds['assistant/message']}, `
    + `tool-result ${kinds['tool/result']}), ${folded.replacements.length} 次替换`)

  console.log('通过: 这份 avox 日志 dsh 全链路装载成功, 可在其上 resume。')
}

main().catch(error => fail(error?.stack ?? String(error)))
