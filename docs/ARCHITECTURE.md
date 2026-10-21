# Architecture

This document describes how soft-ue-cli and the SoftUEBridge plugin fit together in the Mistweaver fork. It is the reference for the roadmap work tracked in #17. Line citations refer to commit `4f1fec7` unless a section states otherwise.

## Repository layout

The repository root holds one Python package (`soft_ue_cli/`), its tests (`tests/`), and documentation (`docs/`, `README.md`, `CHANGELOG.md`). The Unreal plugin source ships inside the Python package as package data, so `pip install soft-ue-cli` carries the plugin that `soft-ue-cli setup` installs into a project (`soft_ue_cli/__main__.py:4786`, which defaults to `Path(__file__).parent / "plugin_data" / "SoftUEBridge"`).

```
soft_ue_cli/            Python package (entry point: soft-ue-cli = soft_ue_cli.__main__:main)
  plugin_data/SoftUEBridge/   Unreal plugin source (C++), copied into projects by `setup`
  skills/               Markdown skills served by `skills` and as MCP prompts
  config/               Offline UE config read/write/audit helpers
  uasset/               Offline .uasset parser
tests/                  pytest suite (testpaths = ["tests"])
docs/                   ARCHITECTURE.md, architecture.svg
pyproject.toml          Hatchling build, version, optional `mcp` extra
```

### Python package

| Module | Purpose |
| --- | --- |
| `__init__.py` | Package marker. |
| `__main__.py` | All argparse definitions (`build_parser`, line 5520), every `cmd_*` handler, `_run_tool` (line 319), `_print_json` (line 246), and `main` (line 9750). About 9,800 lines. |
| `client.py` | HTTP JSON-RPC client for the bridge: `call_tool_ex` / `call_tool` (lines 269, 433), `health_check` (line 444), session identity (`session_descriptor`, line 95), and `session_postmortem` for explaining a vanished editor. |
| `discovery.py` | Resolves the bridge base URL (`get_server_url`, line 54) and the stale-port fallback (`get_forced_port_fallback_url`, line 31). |
| `errors.py` | `ErrorKind` (EXPECTED or UNEXPECTED), `BridgeError`, and the bug-report nudge helpers for CLI and MCP output. |
| `command_catalog.py` | Public command taxonomy and availability metadata behind `soft-ue-cli commands`. |
| `command_aliases.py` | Maps removed flat commands to canonical command families (`COMMAND_ALIAS_PREFIXES`, `REMOVED_COMMAND_MIGRATIONS`). |
| `mcp_schema.py` | Converts the argparse tree into MCP tool schemas (`extract_tools`, line 644), with per-tool overrides. |
| `mcp_server.py` | FastMCP stdio server: registers tools from `mcp_schema` and skills as prompts (`create_server`, line 325). |
| `diagnostics.py` | Offline report helpers used by `diagnose` (build log, Perforce, issue, data-file, handoff summaries). |
| `startup_recovery.py` | Detects and answers the Unreal Editor startup-recovery prompt; `client.py` calls it when a connection fails. |
| `runtime_binary.py` | Plans binary install, update, rollback, and smoke runs for SoftUEBridge in packaged builds (`runtime ...` commands). |
| `surface_selector.py` | Probes the UE 5.8 official MCP endpoint and SoftUEBridge and recommends one (`mcp-surface-status`). |
| `expert_context.py` | Opt-in client for the public Expert Context service (`expert context`). |
| `github.py` | GitHub issue creation for `report-bug` and `request-feature`. |
| `streak.py` | Daily usage streak in `~/.soft-ue-cli/streak.json` that drives the testimonial nudge. |
| `testimonial.py` | `submit-testimonial` command. |
| `umg_layout.py` | UMG layout normalization and comparison (`umg layout ...`). |
| `visual_compare.py` | Offline screenshot comparison for UMG workflows (uses Pillow). |
| `config/` | UE config tooling: INI, `.uproject`/`.uplugin` JSON, and `BuildConfiguration.xml` parsers, hierarchy discovery, diff, merge. |
| `uasset/` | Offline `.uasset` reader: binary primitives, package header/name/import/export tables, tagged properties, Blueprint and External Actor metadata. |
| `skills/__init__.py` | `list_skills()` (parses YAML frontmatter `name` and `description`) and `get_skill(name)` (reads `<name>.md`, rejects path separators). |

### SoftUEBridge plugin

Plugin paths in this document are relative to `soft_ue_cli/plugin_data/SoftUEBridge/Source/` unless shown in full. The descriptor is `soft_ue_cli/plugin_data/SoftUEBridge/SoftUEBridge.uplugin` (plugin `VersionName` 1.3.3, `EnabledByDefault: false`). It declares two modules:

| Module | Type, loading phase | Contents |
| --- | --- | --- |
| `SoftUEBridge` | `DeveloperTool`, `Default` | HTTP server, JSON-RPC types, tool registry, session registry, engine subsystem, and 17 runtime tools that work in editor, PIE, and development game builds. |
| `SoftUEBridgeEditor` | `Editor`, `PostEngineInit` | Editor-only tools, the toolbar extension (`Private/UI/BridgeToolbarExtension.cpp`), and editor utilities (property serializer, graph layout, asset modifier). |

Runtime module directories (`SoftUEBridge/Public` and `SoftUEBridge/Private`):

- `Server/` : `FBridgeServer`, binds the `/bridge` route on UE's `HTTPServer` module.
- `Protocol/` : `FBridgeRequest`, `FBridgeResponse`, schema and tool definition types, JSON-RPC error codes (`BridgeTypes.h`).
- `Session/` : `FBridgeSessionRegistry`, tracks calling LLM sessions, cross-session notices, and `.soft-ue-bridge/sessions.json`.
- `Subsystem/` : `USoftUEBridgeSubsystem` (a `UEngineSubsystem`), owns server lifetime, port selection, and `.soft-ue-bridge/instance.json`.
- `Tools/` : `UBridgeToolBase`, `FBridgeToolRegistry`, `FBridgeToolResult`, plus runtime tools (`query-level`, `batch-call`, `call-function`, `capture-viewport`, `get-logs`, `get-console-var`, `set-console-var`, `spawn-actor`, `set-property`, `get-property`, config get/set/validate, `inspect-anim-instance`, `reload-bridge-module`, `trigger-input`, session channel).
- `Utils/` : JSON object helpers (public only).

The `SoftUEBridge.Build.cs` adds `Private` to `PublicIncludePaths` so the editor module can subclass runtime tool classes.

Editor tool categories live under `SoftUEBridgeEditor/Private/Tools/<Category>/` with matching `Public/Tools/<Category>/` headers: Analysis, Animation, Asset, Blueprint, Build, Cloth, Editor, Material, Niagara, Performance, PIE, Project, References, Rewind, Rig, Scripting, StateTree, Testing, Widget, Write. A grep of `GetToolName()` overrides across both modules finds 144 distinct tool names at commit `4f1fec7`.

### Skills

`soft_ue_cli/skills/` holds 13 Markdown skills with YAML frontmatter. `skills list` and `skills get` read them through `soft_ue_cli/skills/__init__.py`, and `mcp-serve` exposes the same files as MCP prompts.

### Packaging

`pyproject.toml` builds with Hatchling. `[tool.hatch.build] include = ["soft_ue_cli/**"]` covers the package, including `plugin_data/`. A separate `[tool.hatch.build.targets.wheel.force-include]` table lists each skill `.md` file by hand; its comment says to add a line for every new skill. No test checks that this list matches the directory contents. (Speculative: Hatchling normally includes non-Python files inside a listed package, so the force-include table may be redundant. This needs a wheel build to confirm before anyone removes it.)

Dependencies are `httpx` and `Pillow`; the `mcp` extra adds `mcp>=1.2,<2` for `mcp-serve`. The upper bound comes from #88: mcp 2.x removed `mcp.server.fastmcp`, which `mcp_server.py` imports.

### Tests

`tests/` contains one file per module (`test_client.py`, `test_command_catalog.py`, `test_mcp_schema.py`, `test_mcp_server.py`, `test_skills.py`, `test_uasset.py`, and others). C++ coverage is static: `test_plugin_descriptor.py` and `test_plugin_source_compat.py` read plugin source text and assert on patterns (for example, that tools register through `RegisterToolClass` and avoid `REGISTER_BRIDGE_TOOL`). The C++ tree also contains two Automation Spec files under `SoftUEBridgeEditor/Private/Utils/` (`*Spec.cpp`).

### The `cli/soft_ue_cli/` path

The real skills path is `soft_ue_cli/skills/`. There is no `cli/` directory in this repository. The `cli/` prefix comes from a private monorepo: `tests/test_plugin_descriptor.py:38-48` looks for a monorepo `AGENTS.md` and a `plugin/SoftUEBridge` directory and skips or falls back when they are absent, which marks this repository as a public export. Places that still use the monorepo path:

| File | Line | Text |
| --- | --- | --- |
| `README.md` | 212 | ``Skills are **markdown files** at `cli/soft_ue_cli/skills/*.md` `` |
| `tests/test_client.py` | 1 | module docstring `cli/soft_ue_cli/client.py` |
| `tests/test_discovery.py` | 1 | module docstring `cli/soft_ue_cli/discovery.py` |
| `tests/test_github.py` | 1 | module docstring `cli/soft_ue_cli/github.py` |
| `tests/test_main.py` | 1 | module docstring `cli/soft_ue_cli/__main__.py` (also contains a mis-encoded dash) |
| `tests/test_mcp_schema.py` | 1 | module docstring `cli/soft_ue_cli/mcp_schema.py` |
| `tests/test_mcp_server.py` | 1 | module docstring `cli/soft_ue_cli/mcp_server.py` |
| `tests/test_skills.py` | 1 | module docstring `cli/soft_ue_cli/skills` |

No file under `soft_ue_cli/skills/`, `docs/`, or `CHANGELOG.md` uses the wrong path.

## Command flow

This section traces `soft-ue-cli get-console-var r.ScreenPercentage`, a bridge-backed command with one positional argument and no client-side transformation.

### CLI side

1. **Entry.** `main()` (`soft_ue_cli/__main__.py:9750`) builds the parser, parses `sys.argv`, copies the global `--server` and `--timeout` options into `SOFT_UE_BRIDGE_URL` and `SOFT_UE_BRIDGE_TIMEOUT`, repairs MSYS-mangled `asset_path` values, and calls `args.func(args)`. There is no top-level exception handler.
2. **Parser.** `build_parser()` (line 5520) creates one subparser per command on `sub = parser.add_subparsers(dest="command", required=True)` (line 5553). `get-console-var` is defined at lines 5996-6009 with a `name` positional and `set_defaults(func=cmd_get_console_var)`. At the end of `build_parser` (lines 9743-9745), `_add_canonical_command_families` grafts canonical families such as `anim sync-marker inspect` onto copies of legacy parsers, and `_remove_top_level_commands` hides the removed flat names unless `include_removed=True`.
3. **Handler.** `cmd_get_console_var` (line 952) is one line: `_print_json(_run_tool("get-console-var", {"name": args.name}))`. Most bridge handlers follow this shape: build an `arguments` dict from the namespace, call `_run_tool` with the bridge tool name, print the result.
4. **`_run_tool`** (line 319) calls `call_tool_ex`. On `BridgeError` it prints any session notices and `error: <message>` to stderr, appends a `report-bug` hint when `kind` is `UNEXPECTED`, and exits with code 1. On success it prints session notices to stderr, records the usage streak, and returns the result dict.
5. **`call_tool_ex`** (`soft_ue_cli/client.py:269`):
   - **URL.** `get_server_url()` (`soft_ue_cli/discovery.py:54`) resolves in this order: `SOFT_UE_BRIDGE_URL`, then `SOFT_UE_BRIDGE_PORT` (always `127.0.0.1`), then the first `.soft-ue-bridge/instance.json` found walking up from the working directory, then `http://127.0.0.1:8080`. The endpoint is `<base>/bridge`.
   - **Request.** `httpx.post(endpoint, json=payload, timeout=timeout)`. httpx sets `Content-Type: application/json`; the client adds no other headers and no authentication. The timeout is the `timeout` argument or `SOFT_UE_BRIDGE_TIMEOUT` (default 30 seconds).
   - **Payload.** JSON-RPC 2.0:
     ```json
     {"jsonrpc": "2.0", "id": "1", "method": "tools/call",
      "params": {"name": "get-console-var",
                 "arguments": {"name": "r.ScreenPercentage"},
                 "_session": {"id": "...", "label": "...", "origin": "<sha1(cwd)[:8]>",
                              "client": "cli", "pid": 1234, "confidence": "declared|derived"}}}
     ```
   - **Failure handling.** On connect error, timeout, or HTTP error status, the client first tries the stale-port fallback (only when `SOFT_UE_BRIDGE_PORT` is set and `SOFT_UE_BRIDGE_URL` is not, and `instance.json` points elsewhere). On connect error or timeout it then checks for the editor startup-recovery prompt and retries once if it handled one. Otherwise it raises `BridgeError(EXPECTED)` with a message that includes the session postmortem from `sessions.json` when available. HTTP 5xx maps to `UNEXPECTED`, 4xx to `EXPECTED`, and the message is only `HTTP <code>` (the JSON-RPC error body is discarded).
   - **Response.** A non-JSON body raises `UNEXPECTED`. A JSON-RPC `error` object raises `UNEXPECTED` with `error.message`. A `result` with `isError: true` raises `UNEXPECTED` with the first content item's text. Two cases are `EXPECTED` instead: `find-references` `incomplete_fib_index` errors, and (since #86) a JSON payload with `error_code: "plugin_unavailable"`, whose fields are kept on `BridgeError.details`. Otherwise the first `content[0].text` is parsed as JSON and returned; non-JSON text comes back as `{"text": ...}`. `result.session_notices` rides along in `BridgeCallMeta`.
6. **Output.** `_print_json` writes the dict to stdout with `indent=2, ensure_ascii=False`, falling back to ASCII escapes on `UnicodeEncodeError`. Exit code 0.

### Bridge side

1. **Startup.** `USoftUEBridgeSubsystem::Initialize` (`SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:21`) skips commandlets, dedicated servers, and `-unattended` processes. Otherwise it starts the log capture, then `StartServer(ResolvePort())`: port `SOFT_UE_BRIDGE_PORT` or 8080, trying up to 10 consecutive ports (line 111). On success it writes `<ProjectDir>/.soft-ue-bridge/instance.json` with `{"port", "host": "127.0.0.1"}` (line 190); `StopServer` deletes it. PIE start and shutdown hooks restart the HTTP listeners.
2. **HTTP.** `FBridgeServer::Start` (`SoftUEBridge/Private/Server/BridgeServer.cpp:69`) pre-checks the port with a raw socket bound to `127.0.0.1`, then binds `/bridge` for `POST`, `GET`, and `OPTIONS` through `FHttpServerModule` (line 115). `HandleRequest` (line 153) answers `OPTIONS` with permissive CORS headers, answers `GET` with a health document (`name`, `version`, `pid`, `bridge_instance_id`, `tools`, `tool_names`, `module_paths`), and rejects other verbs with 405.
3. **Protocol parsing.** For `POST`, the UTF-8 body goes to `FBridgeRequest::FromJsonString` (`SoftUEBridge/Private/Protocol/BridgeTypes.cpp:25`). Recognized methods are `initialize`, `notifications/initialized`, `shutdown`, `tools/list`, and `tools/call` (line 13). An empty body or invalid JSON returns HTTP 400 with JSON-RPC code -32700.
4. **Game-thread dispatch.** The server looks up the tool's `EBridgeToolExecutionContext` and calls `ScheduleToolRequest` (`BridgeServer.cpp:38`, called at line 275). Every request runs on the game thread through `AsyncTask(ENamedThreads::GameThread, ...)`. Tools that declare `SlateTicker` (`open-asset`, `pie-session`, `run-python-script`) or `PIEWorldTickSafe` (`pie-tick`) are deferred one more step onto `FTSTicker::GetCoreTicker()`. The HTTP response completes asynchronously through the `OnComplete` callback.
5. **`HandleToolsCall`** (line 397) reads `params.name`, `params.arguments` (empty object when absent), and `params._session` into an `FBridgeToolContext`. It sets `GIsRunningUnattendedScript` for the call so modal dialogs auto-answer (line 442). On Windows the call runs inside an SEH guard that converts access violations into JSON-RPC -32603 errors instead of crashing the editor; elsewhere C++ exceptions are caught when enabled.
6. **Registry lookup.** `FBridgeToolRegistry::ExecuteTool` (`SoftUEBridge/Private/Tools/BridgeToolRegistry.cpp:152`) finds the rooted tool instance by name. An unknown name returns a tool error listing every registered tool and loaded module path, with guidance to rebuild the plugin. It collapses `//` in `asset_path`, rejects calls whose execution context does not match (only reachable through nested `batch-call`), touches the session heartbeat (line 224), and calls `Tool->Execute(Arguments, Context)`.
7. **Tool.** `UGetConsoleVarTool::Execute` (`SoftUEBridge/Private/Tools/ConsoleVarTool.cpp:25`) reads the CVar through `IConsoleManager` and returns `FBridgeToolResult::Json({"name", "value"})`.
8. **Response.** `FBridgeToolResult::ToJson` (`SoftUEBridge/Private/Tools/BridgeToolResult.cpp:44`) produces an MCP-style result: `{"content": [{"type": "text", "text": "<serialized JSON>"}], "isError": false}`. `FBridgeSessionRegistry::DrainInto` adds pending `session_notices` (`BridgeServer.cpp:465`), and `FBridgeResponse::ToJsonString` wraps it as `{"jsonrpc": "2.0", "id": "1", "result": {...}}` with HTTP 200. Tool failures use `FBridgeToolResult::Error`, which sets `isError: true` and still returns HTTP 200.

### Tool registration

Registration is explicit and runs at module startup. Each module's `StartupModule` calls `FBridgeToolRegistry::Get().RegisterToolClass<T>()` once per tool:

- Runtime tools: `FSoftUEBridgeModule::StartupModule` (`SoftUEBridge/Private/SoftUEBridgeModule.cpp:25-49`); `get-console-var` is line 35.
- Editor tools: `FSoftUEBridgeEditorModule::StartupModule` (`SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp:300`). Newer animation, rig, cloth, socket, and Niagara tools register later in `RegisterAnimationTools` (line 139), triggered by both `OnPostEngineInit` (line 392) and a one-shot core ticker, because their `StaticClass()` pointers may be invalid at module startup in a freshly rebuilt editor. That function guards each call with `HasTool`.

`RegisterToolClass` (`BridgeToolRegistry.cpp:28`) instantiates the class with `NewObject` in the transient package, reads `GetToolName()`, calls `AddToRoot()` so the plain C++ singleton can hold it without GC collecting it, and stores the class, instance, and owning module name under a critical section. There is no static self-registration macro: `tests/test_plugin_descriptor.py:385-412` asserts that `REGISTER_BRIDGE_TOOL` is absent and that tools use `RegisterToolClass`. `reload-bridge-module` removes tools by module through `RemoveToolsForModule`.

A tool subclasses `UBridgeToolBase` (`SoftUEBridge/Public/Tools/BridgeToolBase.h:39`) and overrides `GetToolName`, `GetToolDescription`, `GetInputSchema`, `GetRequiredParams`, `Execute`, and optionally `GetExecutionContextRequirement`. The input schema feeds `tools/list` only; the CLI never calls `tools/list`, and the bridge does not validate arguments against it.

### Sequence

```mermaid
sequenceDiagram
    participant U as User / agent
    participant M as __main__.py
    participant C as client.py
    participant H as FBridgeServer (HTTP thread)
    participant G as Game thread
    participant R as FBridgeToolRegistry
    participant T as UGetConsoleVarTool

    U->>M: soft-ue-cli get-console-var r.ScreenPercentage
    M->>M: build_parser, parse_args, cmd_get_console_var
    M->>C: _run_tool -> call_tool_ex("get-console-var", {name})
    C->>C: get_server_url (env, instance.json, 8080)
    C->>H: POST /bridge {jsonrpc, id, method: tools/call, params}
    H->>H: FBridgeRequest::FromJsonString
    H->>G: AsyncTask(GameThread) [+ FTSTicker for SlateTicker/PIE tools]
    G->>R: HandleToolsCall -> ExecuteTool(name, args, ctx)
    R->>T: Execute(args, ctx)
    T-->>R: FBridgeToolResult::Json
    R-->>G: result
    G-->>H: {"jsonrpc","id","result":{"content":[...],"isError":false}}
    H-->>C: HTTP 200 JSON
    C->>C: parse content[0].text as JSON
    C-->>M: dict
    M-->>U: stdout JSON, exit 0 (stderr: notices; errors exit 1)
```

### Local commands

Commands that do not need the editor never touch `client.py`. For example, `asset inspect-file` resolves to `cmd_inspect_uasset` (`soft_ue_cli/__main__.py:1416`), which calls `soft_ue_cli/uasset` directly, prints JSON or a table, and exits 1 on `FileNotFoundError`, `UAssetError`, or `ValueError`. Others in this group include `commands`, `skills`, `config`, `setup`, `check-setup` (which does call `health_check`), `diagnose build-log`, `save-script`, and `list-scripts`. Some local commands compose bridge calls: `diagnose asset` and `diagnose probe` call `_run_tool` several times and aggregate the results.

### Error contract

- CLI exit codes: 0 on success, 1 on any error. Errors go to stderr as `error: <message>`, followed by a `report-bug` suggestion when the error is `UNEXPECTED`.
- `errors.py` defines the `EXPECTED`/`UNEXPECTED` split and one bridge error code, `PLUGIN_UNAVAILABLE` (#86). It has no general error-code enumeration. Structured `error_code` values exist ad hoc in `__main__.py` for readiness checks (`bridge_not_ready`, `bridge_unreachable`, `http_404_not_bridge`, and others near line 655).
- `plugin_unavailable`: `UBridgeToolBase::PluginUnavailable` (`SoftUEBridge/Private/Tools/BridgeToolBase.cpp:89`) builds `{"success": false, "error_code": "plugin_unavailable", "plugin", "command", "message", "recovery"}`. Since #86 it keeps that JSON as the result text and sets `isError: true`. `call_tool_ex` recognizes the payload through `parse_structured_error` (`soft_ue_cli/errors.py:41`) and raises `BridgeError(EXPECTED)` with the message and recovery, so the CLI prints both to stderr and exits 1 without a `report-bug` hint. Callers: `run-python-script` when PythonScriptPlugin is not loaded, and the nine Mutable edit tools in `EditCustomizableObjectGraphTool.cpp` when the asset fails to load and `MutableIntrospectionUtils::IsMutablePluginEnabled()` is false.
- MCP: `mcp_server.py` returns errors as a JSON string `{"error": "Tool '<name>' failed: ..."}` in the tool result, adds `bug_report_hint` for `UNEXPECTED` errors, and copies any `BridgeError.details` fields (such as `error_code`, `plugin`, `command`, `message`, and `recovery`) beside `error`. It does not raise MCP protocol errors.

## Commands catalog

### Storage

`soft_ue_cli/command_catalog.py` builds the catalog at call time; nothing is cached or stored on disk.

- `_entry(...)` (line 48) creates one record and validates `layer`, `category`, and `status` against the `LAYERS`, `CATEGORIES`, and `STATUSES` sets (lines 13-36), raising `ValueError` on unknown values. Fields: `name`, `summary`, `layer` (`offline`, `bridge`, `removed`, `workflow`, `skill`, `support`), `category` (20 values such as `animation`, `asset`, `blueprint`, `umg`, `verify`), `requires_bridge`, `requires_editor`, `requires_pie`, `required_plugins`, `status` (`canonical`, `removed`, `deprecated`), `canonical_command`, and `examples`.
- `_plugin(name, module, note)` (line 39) creates a `required_plugins` item with `required: True`.
- There is no `aliases` field. Alias data lives in `command_aliases.py`: `COMMAND_ALIAS_PREFIXES` maps canonical paths (for example `("anim", "sync-marker", "inspect")`) to removed flat names, and `REMOVED_COMMAND_MIGRATIONS` maps removed names to canonical command strings. The catalog surfaces these as `status: "removed"` entries with `canonical_command` set.

`_build_catalog` (line 1576) merges three sources:

1. **Argparse.** `_argparse_command_summaries` (line 1491) imports `build_parser`, walks every subparser path, and records the help text. Each path gets `_default_entry` (line 1466), which infers metadata by rule: `requires_bridge` is true unless the name is in `_CLIENT_SIDE_COMMANDS` (line 1388); `category` comes from name patterns in `_infer_category` (line 1442); `requires_pie` from `_PIE_COMMANDS` (line 1411); `required_plugins` from name patterns in `_plugin_requirements_for_command` (line 1428, for example any name containing `statetree` requires the StateTree plugin); `requires_editor` from the inferred category, `_EDITOR_OPTIONAL_COMMANDS`, or an editor-only plugin.
2. **Migrations.** Canonical names that replace a removed flat command take that command's inferred metadata, with category and editor requirement adjusted by family (`_apply_canonical_family_metadata`, line 1539).
3. **Explicit entries.** `_EXPLICIT_ENTRIES` (line 83) overrides inference for 135 commands and families with hand-written summaries, requirements, and examples.

Removed entries are added only when `include_removed=True`.

### The `commands` command

`cmd_commands` (`soft_ue_cli/__main__.py:261`) supports `--json`, `--category`, `--requires-bridge`, `--plugin`, `--include-removed`, and `--probe`. With `--json` it prints `command_metadata_as_json()` (`{"schema": "soft-ue.commands.v1", "commands": [...]}`) after filtering. Without `--json` it prints a fixed-width table (command, layer, category, bridge, status, canonical). `--probe` does not contact the bridge: it sets `available: "unknown"` and a note saying probing is not enabled (`command_catalog.py:1620-1633`). Each call rebuilds the full argparse tree to compute the catalog.

### MCP tool schemas

`mcp-serve` (`cmd_mcp_serve`, `soft_ue_cli/__main__.py:4946`) imports `mcp_server.run_server`, which builds a FastMCP server over stdio. Schemas come from argparse only. `mcp_schema.py` and `mcp_server.py` never import `command_catalog`, so catalog fields such as `requires_editor`, `required_plugins`, and `examples` do not reach MCP clients.

`extract_tools()` (`soft_ue_cli/mcp_schema.py:644`):

1. Calls `build_parser(include_removed=True)`, so removed flat names are also exposed as tools.
2. For each top-level subparser, `_tool_def` (line 632) runs `_extract_one` (line 514), which maps every argparse action to a JSON Schema property through `_argparse_type_to_json` (line 485): `store_true`/`store_false` become `boolean`, `type=int` becomes `integer`, `type=float` becomes `number`, everything else becomes `string`; `choices` become `enum`; `help` and `default` carry over. Positionals and `required=True` options go in `required`.
3. One level of nested subcommands is flattened into a single tool with a `subcommand` enum and the union of child properties (`_extract_nested_subcommands`, line 560).
4. Names in `EXCLUDED_COMMANDS` (line 46: `anim`, `asset`, `blueprint`, `capture`, `cloth`, `umg`, `mutable`, `statetree`, `session`, `skills`, `runtime`, and others) are expanded into one tool per leaf path instead, named with spaces (for example `anim sync-marker inspect`). `mcp-serve` and `await-bridge` have no subcommands and are dropped.
5. `EXTRA_BRIDGE_TOOLS` (line 368) adds `get-config-value`, `set-config-value`, and `validate-config-key` as bridge tools when no argparse command has the same name.
6. `MCP_TOOL_ALIASES` (line 35) adds removed names that point to a canonical tool not already present, by copying the canonical schema.

`_apply_tool_overrides` (line 606) then merges `TOOL_OVERRIDES` (line 91) and `MCP_ALIAS_OVERRIDES` (line 36). Overrides can change or add properties, remove properties (`properties_remove`), and adjust `required` (`required_add`, `required_remove`). This is the only way MCP parameters become structured JSON. On the CLI these values are strings that handlers parse; in MCP, overrides retype them, for example `batch-call.calls` and `spawn-actor.location` as `array`, `call-function.args` as `object`, and `set-property.value` as the non-standard `any`. `_build_signature` (`soft_ue_cli/mcp_server.py:56`) converts the schema into a keyword-only Python signature that FastMCP introspects: `array` becomes `list`, `object` becomes `dict`, `any` becomes `typing.Any`, optional parameters become `Optional[...]`.

`create_server()` (`soft_ue_cli/mcp_server.py:325`) registers each tool through one of two adapters:

- **Bridge adapter** (`_make_tool_fn`, line 129) for names without a space that are not in `mcp_schema.CLIENT_SIDE_COMMANDS` (line 67). It drops `None` values, applies `_BRIDGE_TOOL_NAME_MAP` (for example `class-hierarchy` to `get-class-hierarchy`) and `_PARAM_RENAMES`, and calls `call_tool_ex` directly with the MCP arguments. The CLI `cmd_*` handler does not run, so any argument shaping a handler performs has to be repeated here or in the C++ tool.
- **Client adapter** (`_make_client_tool_fn`, line 243) for client-side names and every multi-word name. It builds an `argparse.Namespace` from schema defaults and the call's arguments, runs the original `cmd_*` handler with stdout and stderr captured, converts `SystemExit` into `{"error": ...}`, and returns the captured JSON text.

Skills become prompts (lines 353-372): for each `list_skills()` entry, a zero-argument function returns `get_skill(name)`, registered with `Prompt.from_function` under the skill's frontmatter name and description.

At commit `4f1fec7`, `extract_tools()` returns 261 tools and the catalog has 240 entries.

### Routing mismatches

Static analysis at commit `4f1fec7` finds top-level MCP tools that the bridge adapter forwards to the bridge even though the CLI implements them locally, and no C++ tool has these names: `automation`, `diagnose`, `expert`, `save-script`, `list-scripts`, `delete-script`, and `query-ue-knowledge`. These calls would return the bridge's "Unknown tool" error through MCP (not verified against a running editor). The cause is that `CLIENT_SIDE_COMMANDS` in `mcp_schema.py` and `_CLIENT_SIDE_COMMANDS` in `command_catalog.py` are separate hand-kept lists that disagree with each other and with the handlers. The catalog also reports `save-script`, `list-scripts`, `delete-script`, and `query-ue-knowledge` as `requires_bridge: true`.

### Adding a command

1. **C++ tool** (skip for local-only commands). Subclass `UBridgeToolBase` in `SoftUEBridge/Private/Tools/` (works in game builds) or `SoftUEBridgeEditor/Private/Tools/<Category>/` with a header in `Public/Tools/<Category>/` (editor only). Implement `GetToolName`, `GetToolDescription`, `GetInputSchema`, `GetRequiredParams`, and `Execute`; override `GetExecutionContextRequirement` when the tool must run on the Slate ticker or a PIE-tick-safe point. Use `FBridgeToolResult::Json` for success and `FBridgeToolResult::Error` for failures.
2. **Register it** with `Registry.RegisterToolClass<UYourTool>()` in the module's `StartupModule`. In the editor module, put new UCLASS tools in `RegisterAnimationTools` with a `HasTool` guard if they fail to register at startup. Do not use a static registration macro.
3. **Add module or plugin dependencies** to the module's `.Build.cs` and, for optional plugins, to `SoftUEBridge.uplugin` with `"Optional": true` and `TargetAllowList: ["Editor"]` where it applies.
4. **Argparse.** Add a subparser in `build_parser` (`soft_ue_cli/__main__.py`) with `help`, a `description` containing an `EXAMPLES:` block, and `set_defaults(func=cmd_your_command)`. Put it under a canonical family when one exists.
5. **Handler.** Add `cmd_your_command` that builds the arguments dict and calls `_print_json(_run_tool("your-tool", arguments))`, or does the local work directly.
6. **Catalog.** Add an `_entry` to `_EXPLICIT_ENTRIES` in `command_catalog.py` when the inferred layer, category, editor, PIE, or plugin requirements would be wrong. Add local-only names to `_CLIENT_SIDE_COMMANDS`.
7. **MCP.** Add local-only top-level names to `CLIENT_SIDE_COMMANDS` in `mcp_schema.py`. Add a `TOOL_OVERRIDES` entry for any argument that should be a JSON array, object, or `any` in MCP, or whose required set differs. If the CLI name differs from the bridge name, add it to `_BRIDGE_TOOL_NAME_MAP`; if an argument name differs, add `_PARAM_RENAMES` in `mcp_server.py`. New nested families also need their root in `EXCLUDED_COMMANDS`.
8. **Aliases.** When renaming or folding a flat command into a family, add the mapping to `COMMAND_ALIAS_PREFIXES` or `REMOVED_COMMAND_MIGRATIONS` in `command_aliases.py`.
9. **Tests.** Cover argument parsing in `tests/test_main.py`, the schema in `tests/test_mcp_schema.py`, catalog metadata in `tests/test_command_catalog.py`, and registration with a source-pattern test in `tests/test_plugin_descriptor.py`.
10. **Docs.** Update `README.md` and `CHANGELOG.md`. For a new skill, add the `.md` file to `soft_ue_cli/skills/` and a line to the `force-include` table in `pyproject.toml`.

## Fork changes since upstream v1.45.0

Inventory taken 2026-10-13 against fork `main` at `4f1fec7` (`4f1fec744e71bb234113b5093cf041518656fb4e`). The baseline is the upstream tag `softdaddy-o/soft-ue-cli` `v1.45.0` (`6239e83`, "sync: cli-v1.45.0", 2026-08-03). The tag is an ancestor of fork `main`; the fork merged it on 2026-08-07 in `173a4fc`.

### Summary

| Measure | Value |
| --- | --- |
| Commits in `v1.45.0..main` | 42 |
| Non-merge commits | 25 (all authored by Mistweaver) |
| Merge commits | 17 |
| Upstream `main` commits after `v1.45.0` | 0 (upstream `main` still points at `6239e83`, 2026-08-03) |
| Fork commits taken from upstream after the tag | None |
| Fork commits patch-equivalent to upstream (`git cherry`) | None |
| Net diff `v1.45.0..main` | 34 files, +5626 / -117 lines |
| Open pull requests | Only the Phase 0 documentation PRs #81 to #83, opened alongside this inventory |
| Closed unmerged pull requests | None |

Package version in `pyproject.toml` is still `1.45.0`, and the `Unreleased` section of `CHANGELOG.md` is empty. No fork commit since the tag touches `CHANGELOG.md`.

### Feature groups

| Group | Description | PRs | Commits | CLI commands | In `CHANGELOG.md` |
| --- | --- | --- | --- | --- | --- |
| Control Rig tools | Read-only inspection of Control Rig hierarchies, control values, and RigVM graphs. | #13 | `2899c7f` | New: `rig inspect`, `rig control get`, `rig graph inspect` | No |
| Niagara System inspection | Read-only dump of a Niagara System: emitters, ordered module stack per script stage, module input sources, renderers and their materials, curves, scalability, and user parameters. | #15, #14 | `ffe8044` (#15); `2879bbf`, `981fae4`, `f50d63a` (#14) | New: `niagara system inspect` | No |
| Material root connections and resolved settings | `query-material` reports what each material output (BaseColor, EmissiveColor, Opacity, and so on) connects to, and resolves shading model and blend mode to names. | #15, #14 | `f070008` (#15); `62fe2e9` (#14) | Changed output: `query-material` (no new flags) | No |
| `set-property` inherited and native component overrides | `set-property` can override properties on components a Blueprint inherits from an ancestor SCS node or from native C++. | #15, #14 | `f505b6c` (#15); `62fe2e9` (#14) | Changed behavior: `set-property` (no new flags) | No |
| Cloth build fixes and `.uplugin` dependency | Clears UE 5.7 cloth deprecation warnings, restores UE 5.7 compatibility for cloth Dataflow and unbind APIs, fixes a `LoadSkeletalMesh` unity-build collision, and declares the optional `ChaosClothAssetEditor` plugin dependency. | #12, #16; two commits without a PR | `fad6b56` (#12); `c4870ac` (#16); `f12f48c`, `8338d96` (direct to `main`) | None new; affects the `cloth` family build | No |
| Unreal Insights trace analysis | `insights-analyze` reads traces through the TraceServices providers and gains frame, timer, call tree, counter, CSV profiler, thread, and bottleneck analyses. `insights-capture` now starts and stops tracing through `FTraceAuxiliary`, so paths with spaces work. | None (direct to `main`) | `9b3a476`, `0dc06d7`, `7176eab`, `2704edc` | Changed: `insights-analyze`, `insights-capture` | No |
| UE 5.8 and 5.7 compatibility (upstreamed in equivalent form) | Builds against UE 5.8 `FSharedString` JSON keys, migrates UE 5.8 and UE 5.7 deprecated APIs, fixes an anim montage unity-build collision, guards a client `KeyError`, and fixes a testimonial backslash. | #6, #7, #8, #9, #10, #11; one commit without a PR | `7e65569` (#6); `39bb6ba` (direct); `f1e523e`, `036e183` (#7); `ff136d7` (#8); `3208b88` (#9); `9289afa` (#10); `beb22ab`, `2ef63a3` (#11) | None | Yes, as upstream entries in 1.39.0 through 1.42.0 |

### Group notes

**Control Rig tools.** Adds the `rig` command family to `command_catalog.py`, `__main__.py`, and the MCP command families in `mcp_schema.py`. Flags: `rig inspect` takes `--name-filter`, `--element-type`, `--transforms`, `--include-settings`, and `--include-offsets`; `rig control get` takes `--control` and `--value-type`; `rig graph inspect` takes `--graph`, `--node-filter`, `--include-pins`, and `--include-links`. Bridge tools `rig-inspect`, `rig-control-get`, and `rig-graph-inspect` register in `SoftUEBridgeEditorModule.cpp`. The `.uplugin` gains editor-only `ControlRig` and `RigVM` dependencies, and `SoftUEBridgeEditor.Build.cs` gains the matching modules.

**Niagara System inspection.** The work landed through two PRs with overlapping history. PR #15 (merged 2026-09-16) carries `ffe8044`, a rewritten single commit with the full bridge tool `niagara-system-inspect`, its serializer, the `Niagara` `.uplugin` dependency, and module registration. PR #14 (merged 2026-10-05) carries the original commit series. Its merge (`4f1fec7`) changed only Python files relative to `1d42022`: the `niagara` command family in `__main__.py`, `command_catalog.py`, and `mcp_schema.py`, plus tests. Flags on `niagara system inspect`: `--emitter`, `--no-modules`, `--no-renderers`, `--no-parameters`, `--skip-disabled-modules`, `--include-module-inputs`, `--max-dynamic-input-depth`, and `--include-curves`.

**Material root connections and resolved settings.** Bridge-side change in `QueryMaterialTool.cpp` (`ExtractRootConnections`, `ExtractMaterialSettings`). The shading model field is a bitmask, and the tool now decodes it to a name. `62fe2e9` in PR #14 is an earlier combined version of this change and the `set-property` fix; `f070008` and `f505b6c` in PR #15 split them.

**`set-property` inherited and native component overrides.** Bridge-side change in `SetPropertyTool.cpp`. Components declared by an ancestor Blueprint's SCS node are overridden through the `InheritableComponentHandler` template for that node. Native components are handled separately.

**Cloth build fixes and `.uplugin` dependency.** `ChaosClothAssetEditor` is declared as optional and editor-only. `f12f48c` and `8338d96` were committed straight to `main` on 2026-08-07 with no PR; they add tests in `tests/test_plugin_source_compat.py` and `tests/test_plugin_descriptor.py`.

**Unreal Insights trace analysis.** All four commits went straight to `main` on 2026-08-07 with no PR. New `--analysis-type` values: `frame_stats`, `top_functions`, `call_tree`, `counters`, `csv_stats`, `threads`, and `bottlenecks` (upstream offered only `basic_info`). New flags: `--timer-name`, `--direction` (`callees` or `callers`), `--max-depth`, `--column-filter`, `--top-n`, `--start-time`, `--end-time`, and `--hitch-threshold-ms`. `7176eab` fixes four runtime defects found against a real trace, including a TraceServices read-scope crash and an `insights-capture` path bug that silently wrote no trace. `2704edc` ranks bottleneck timers by self time over the full timer set.

**UE 5.8 and 5.7 compatibility.** Upstream released equivalent changes under its own SHAs, so the net diff against `v1.45.0` is small. The only remaining fork-specific difference is the version guard form: the fork uses `ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)` where upstream uses `ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8`. The difference appears in `SoftUEBridgeEditorModule.cpp`, `EditCustomizableObjectGraphTool.cpp`, `MutableIntrospectionUtils.cpp`, and `SetNodePositionTool.cpp`. These commits need no changelog backfill.

### Changelog backfill input

Issue #29 should add entries for the following groups, none of which `CHANGELOG.md` records today:

| Group | Suggested section |
| --- | --- |
| Control Rig tools | Added |
| Niagara System inspection | Added |
| Unreal Insights trace analysis (new analysis types and flags) | Added |
| Material root connections and resolved settings | Added |
| `set-property` inherited and native component overrides | Fixed |
| Insights runtime defects and bottleneck ranking (`7176eab`, `2704edc`) | Fixed |
| Cloth build fixes and `ChaosClothAssetEditor` dependency | Fixed |
| UE 6 forward version guards | Fixed (optional; one line) |

### Merge commits

| SHA | Date | Merges |
| --- | --- | --- |
| `4f1fec7` | 2026-10-05 | PR #14 `feat/niagara-system-inspect` |
| `1d42022` | 2026-09-16 | PR #16 `fix/uplugin-chaosclothasseteditor-dep` |
| `92fe38c` | 2026-09-16 | PR #15 `feat/niagara-inspect-tools` |
| `4164632` | 2026-08-28 | PR #13 `feat/rig-tools` |
| `ef55ad0` | 2026-08-20 | PR #12 `pr/ue57-cloth-deprecation-warnings` |
| `173a4fc` | 2026-08-07 | Upstream `origin/main` at `v1.45.0` |
| `14b59a8` | 2026-07-05 | PR #11 `fix/client-keyerror-testimonial-backslash` |
| `ad454ac` | 2026-07-05 | PR #10 `fix/ue58-deprecation-version-guards` |
| `0cf3b03` | 2026-07-05 | PR #9 `fix/ue58-deprecation-warnings` |
| `8ba7627` | 2026-07-05 | PR #8 `fix/anim-montage-unity-build-collision` |
| `48e3380` | 2026-07-05 | PR #7 `ue5.8-jsonobject-sharedstring-compat` |
| `0cfe873` | 2026-07-04 | `fork/main` into local `main` |
| `3025184` | 2026-07-04 | Upstream `origin/main` |
| `40a6d81` | 2026-06-15 | PR #6 `fix/asset-material-deprecation-warnings` |
| `778c298` | 2026-06-15 | Upstream `origin/main` |
| `5f5713c` | 2026-06-03 | Upstream `main` at `cli-v1.36.0` |
| `47e0f79` | 2026-05-29 | PR #5 `feat/metasound-inspect` (content already in `v1.45.0`) |

### Pull requests

At inventory time the fork had 16 merged pull requests, listed below, plus the open Phase 0 documentation PRs #81 to #83. None had been closed without merging.

| PR | Merged | Title | Status against `v1.45.0` |
| --- | --- | --- | --- |
| #16 | 2026-09-16 | fix(plugin): declare ChaosClothAssetEditor dependency in .uplugin | Fork only |
| #15 | 2026-09-16 | feat(plugin): Niagara inspection tools, material root connections, inherited-component property writes | Fork only |
| #14 | 2026-10-05 | feat(niagara): add read-only Niagara System inspection tool | Fork only |
| #13 | 2026-08-28 | feat(rig): read-only Control Rig inspection tools | Fork only |
| #12 | 2026-08-20 | fix(plugin): clear UE 5.7 cloth deprecation warnings in ClothTools | Fork only |
| #11 | 2026-07-05 | fix(cli): content KeyError guard, testimonial backslash, and descriptor test regression | Equivalent upstream (1.41.0) |
| #10 | 2026-07-05 | fix(plugin): version-guard the UE 5.8 deprecated-API migration for 5.7 | Equivalent upstream, except the guard form |
| #9 | 2026-07-05 | fix(plugin): migrate UE 5.8-deprecated APIs in editor module | Equivalent upstream (1.41.0) |
| #8 | 2026-07-05 | fix(plugin): resolve SegmentToJson unity-build collision in anim montage tools | Equivalent upstream (1.41.0) |
| #7 | 2026-07-05 | Fix SoftUEBridge for UE 5.8 FJsonObject FSharedString keys | Equivalent upstream (1.40.0, 1.42.0) |
| #6 | 2026-06-15 | fix: resolve C4996 deprecation warnings in asset/material query tools | Equivalent upstream (1.39.0) |
| #5 | 2026-05-29 | feat(metasound): read-only `metasound inspect` bridge tool | Commits in `v1.45.0` |
| #4 | 2026-04-13 | fix(tests): repair 3 broken test assertions | Commits in `v1.45.0` |
| #3 | 2026-04-13 | feat(plugin): default SoftUEBridge module to DeveloperTool | Commits in `v1.45.0` |
| #2 | 2026-04-13 | fix(run-python-script): resolve --script-path locally | Commits in `v1.45.0` |
| #1 | 2026-04-10 | Fix input validation errors and minor bugs | Commits in `v1.45.0` |

Seven fork commits reached `main` without a pull request: `39bb6ba`, `f12f48c`, `8338d96`, `9b3a476`, `0dc06d7`, `7176eab`, and `2704edc`.

## Bridge HTTP listener

This section records how the bridge HTTP listener behaves at commit `4f1fec7`. It is the baseline for the security work in #33 through #39.

Paths under `Source/` are relative to `soft_ue_cli/plugin_data/SoftUEBridge/`. Paths under `Engine/` refer to the local UE 5.7.4 install and sit outside this repository.[^engine]

### Components

| Piece | Location | Role |
| --- | --- | --- |
| `USoftUEBridgeSubsystem` | `Source/SoftUEBridge/Public/Subsystem/SoftUEBridgeSubsystem.h:14` | `UEngineSubsystem` that owns the server, picks the port, and writes `instance.json`. |
| `FBridgeServer` | `Source/SoftUEBridge/Private/Server/BridgeServer.cpp:69` | Registers the `/bridge` route on the engine `HTTPServer` module and dispatches JSON-RPC. |
| Engine `HTTPServer` module | `Source/SoftUEBridge/SoftUEBridge.Build.cs:25` | Owns the socket. The plugin opens no listening socket of its own. |
| CLI discovery | `soft_ue_cli/discovery.py:54` | Resolves the bridge URL. |
| CLI client | `soft_ue_cli/client.py:269` | Sends JSON-RPC over `httpx`. The MCP server uses the same client (`soft_ue_cli/mcp_server.py:21`, `:187`). |

### Bind address

The plugin intends a loopback bind, but the engine decides the real bind address.

- `USoftUEBridgeSubsystem::StartServer` passes the literal `"127.0.0.1"` to `FBridgeServer::Start` (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:129`). The header default is the same (`Source/SoftUEBridge/Public/Server/BridgeServer.h:24`).
- `FBridgeServer::Start` uses that address only for a throwaway port probe: it creates a socket, binds `127.0.0.1:<port>`, and closes it (`Source/SoftUEBridge/Private/Server/BridgeServer.cpp:82-104`). It also uses the address in a log line (`:135`).
- The real listener comes from `FHttpServerModule::GetHttpRouter(ServerPort)` (`Source/SoftUEBridge/Private/Server/BridgeServer.cpp:107`). The bind address is never passed to the engine.
- The engine reads the address from `GEngineIni`, section `[HTTPServer.Listeners]`, key `DefaultBindAddress`, then applies any `ListenerOverrides=(Port=...,BindAddress=...)` entry for the same port (`Engine/Source/Runtime/Online/HTTPServer/Private/HttpServerConfig.cpp:11-54`).
- With no ini value, the code default is `"localhost"` (`Engine/Source/Runtime/Online/HTTPServer/Private/HttpServerConfig.h:13`). `"localhost"` maps to `SetLoopbackAddress()`, `"any"` maps to `SetAnyAddress()`, and any other string is parsed as an IP (`Engine/Source/Runtime/Online/HTTPServer/Private/HttpListener.cpp:62-83`).
- Neither `Engine/Config/BaseEngine.ini` in UE 5.7.4 and 5.8 nor any ini in this repository sets `DefaultBindAddress` or `ListenerOverrides`. The effective default is therefore loopback.
- A project or platform `DefaultEngine.ini` that sets `DefaultBindAddress=any` (or a port override) exposes the bridge on every interface. The plugin does not detect this. It still logs `http://127.0.0.1:<port>/bridge` (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:139`) and writes `"host": "127.0.0.1"` to `instance.json` (`:210`).
- `GetHttpRouter` is called without `bFailOnBindFailure`. On a failed bind the engine still returns a router (`Engine/Source/Runtime/Online/HTTPServer/Private/HttpServerModule.cpp:167-178`), so the plugin reports `Running` and writes `instance.json` even when nothing is listening (`Source/SoftUEBridge/Private/Server/BridgeServer.cpp:108-133`).
- If another engine system already holds a listener on the chosen port, the engine reuses it and the bridge route joins that listener with its bind settings (`Engine/Source/Runtime/Online/HTTPServer/Private/HttpServerModule.cpp:125-151`).

### Port selection

- Default: `8080` (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:187`).
- Override: the `SOFT_UE_BRIDGE_PORT` environment variable of the Unreal process, read through `FPlatformMisc::GetEnvironmentVariable`. Values that do not parse to a positive integer fall back to 8080 (`:178-188`).
- No other source exists. The plugin reads no command-line switch, ini key, or project setting for the port.
- Fallback: `StartServer` tries `Port` through `Port + 9` (10 attempts) and stops early above 65535 (`:118-126`). The availability test is the loopback probe described above. Each failure logs `Port <n> unavailable`, and exhaustion logs an error and leaves the server stopped (`:142-147`).
- `RestartServer` re-resolves the port (`:172-176`). The editor toolbar calls it and shows the port (`Source/SoftUEBridgeEditor/Private/UI/BridgeToolbarExtension.cpp:155`, `:169`). `StartServer(int32 Port = 8080)` is also `BlueprintCallable` (`Source/SoftUEBridge/Public/Subsystem/SoftUEBridgeSubsystem.h:24`).
- Multiple instances: the second process on the same machine lands on 8081, and so on. All instances of one project share one `instance.json` (see Instance discovery).

### Authentication

None. The request path performs no token, header, `Origin`, or `Host` check.

- `HandleRequest` dispatches on verb only (`Source/SoftUEBridge/Private/Server/BridgeServer.cpp:153-286`).
- CORS is fully open. `OPTIONS` returns `Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: GET, POST, OPTIONS`, and `Access-Control-Allow-Headers: Content-Type` (`:156-165`). Every `GET` and JSON-RPC response carries `Access-Control-Allow-Origin: *` (`:198`, `:473`).
- The caller identity in `params._session` (`id`, `label`, `origin`, `client`, `pid`) is self-asserted and used for session bookkeeping only (`:425-435`).
- The JSON-RPC error codes are the standard set plus `ServerError = -32000`. No `unauthorized` code exists (`Source/SoftUEBridge/Public/Protocol/BridgeTypes.h:13-20`).
- The CLI sends no credentials. Its POST carries only the JSON body (`soft_ue_cli/client.py:282-296`).

Reachable tools include `call-function`, `set-property`, `set-config-value`, `set-console-var`, `spawn-actor`, and `trigger-input` in every build that has the module (`Source/SoftUEBridge/Private/SoftUEBridgeModule.cpp:30-46`), plus `exec-console-command` and `run-python-script` in the editor (`Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp:349`, `:377`).

### Request handling

| Item | Behavior | Citation |
| --- | --- | --- |
| Route | One route, `/bridge`. No `/health` path exists. | `Source/SoftUEBridge/Private/Server/BridgeServer.cpp:115-119` |
| `OPTIONS` | CORS preflight, `204 No Content`. | `:156-165` |
| `GET` | Health JSON: `name`, `version`, `running`, `pid`, `started_at`, `bridge_instance_id`, `tools`, `tool_names`, `module_paths` (plugin binary paths). | `:168-202` |
| `POST` | JSON-RPC 2.0. Methods: `initialize`, `notifications/initialized`, `shutdown`, `tools/list`, `tools/call`. | `:290-310` |
| Other verbs | `405` with JSON-RPC `InvalidRequest`. | `:204-208` |
| Empty or invalid body | `400` with `ParseError`. | `:220-231` |
| Notifications | `202 Accepted`, empty body. | `:262-268` |
| Body size | No plugin-side limit. The engine reads `Content-Length` bytes as a clamped `int32`. | `Engine/Source/Runtime/Online/HTTPServer/Private/HttpConnectionRequestReadContext.cpp:192-199` |
| Connection timeouts | Engine: 5 s read/write idle, 15 s keep-alive. | `Engine/Source/Runtime/Online/HTTPServer/Private/HttpConnection.h:252`, `:255` |
| Execution timeout | None on the server. A slow tool blocks the game thread and every queued request. | `Source/SoftUEBridge/Private/Server/BridgeServer.cpp:240-283` |
| Client timeout | 30 s default, `SOFT_UE_BRIDGE_TIMEOUT` or `--timeout` overrides; health check 5 s. | `soft_ue_cli/client.py:280`, `:444`; `soft_ue_cli/__main__.py:9762-9763` |

Threading: `HandleRequest` posts every parsed request to the game thread with `AsyncTask(ENamedThreads::GameThread, ...)` (`Source/SoftUEBridge/Private/Server/BridgeServer.cpp:273-283`). Tools marked `SlateTicker` or `PIEWorldTickSafe` run one step later on `FTSTicker::GetCoreTicker()` (`:38-59`). Tool calls run with `GIsRunningUnattendedScript = true` to suppress modal dialogs (`:442`), and Windows SEH faults turn into JSON-RPC errors (`:445-455`). The engine ticks listeners from `FHttpServerModule::Tick` (`Engine/Source/Runtime/Online/HTTPServer/Private/HttpServerModule.cpp:182-194`), which runs on the game thread.[^tick]

In the editor, the subsystem calls `StartAllListeners()` again after `PostPIEStarted` and `ShutdownPIE` to revive listeners that PIE disrupts (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:58-108`).

### Build configurations

Descriptor (`soft_ue_cli/plugin_data/SoftUEBridge/SoftUEBridge.uplugin`):

- `EnabledByDefault: false` (`:16`). A project enables the plugin in its `.uproject`.
- `SoftUEBridge` module: `Type: DeveloperTool`, `LoadingPhase: Default` (`:105-109`). This module holds the server.
- `SoftUEBridgeEditor` module: `Type: Editor`, `LoadingPhase: PostEngineInit` (`:110-114`). It registers editor-only tools.
- Neither module declares `PlatformAllowList`, `PlatformDenyList`, `TargetAllowList`, or `TargetConfigurationDenyList`. Only the editor-side dependency plugins carry `TargetAllowList: ["Editor"]` (`:17-103`).

UnrealBuildTool builds a `DeveloperTool` module when `bBuildDeveloperTools` is true (`Engine/Source/Programs/UnrealBuildTool/System/ModuleDescriptor.cs:820-821`). That flag defaults to true for Editor and Program targets and for any configuration other than Test and Shipping (`Engine/Source/Programs/UnrealBuildTool/Configuration/TargetRules.cs:1134`).

The server code has no `UE_BUILD_SHIPPING`, `UE_BUILD_DEVELOPMENT`, or `UE_BUILD_TEST` guard. `#if WITH_EDITOR` blocks cover only the PIE recovery hooks (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:17-19`, `:40-42`, `:67-109`) and editor-only tool paths such as `reload-bridge-module` (`Source/SoftUEBridge/Private/Tools/ReloadBridgeModuleTool.cpp:30-31`).

| Target and configuration | Module built | Server starts |
| --- | --- | --- |
| Editor, any configuration | Yes | Yes, except commandlets and `-unattended` runs |
| Game or Client, Development | Yes | Yes |
| Game or Client, DebugGame | Yes | Yes |
| Game or Client, Test | No, unless the target sets `bBuildDeveloperTools = true` | Same as build |
| Game or Client, Shipping | No, unless the target sets `bBuildDeveloperTools = true` | Same as build |
| Server, Development or DebugGame | Yes | No (`IsRunningDedicatedServer()`) |

Startup: `USoftUEBridgeSubsystem` derives from `UEngineSubsystem` (`Source/SoftUEBridge/Public/Subsystem/SoftUEBridgeSubsystem.h:14`), so it initializes with the engine in both editor and packaged processes.[^subsystem] `Initialize` returns early when `IsRunningCommandlet()`, `IsRunningDedicatedServer()`, or `FApp::IsUnattended()` is true (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:29-33`). Otherwise it starts log capture and calls `StartServer(ResolvePort())` (`:37-39`). `Deinitialize` stops the server and deletes `instance.json` (`:45-56`). A packaged Development build launched with `-unattended`, as CI often does, runs without a bridge.

The README states the same build matrix (`README.md:1047`, `README.md:972`), and `soft_ue_cli/runtime_binary.py:12` treats only Development and DebugGame as supported packaged configurations.

### Instance discovery

Plugin side:

- Writer: `USoftUEBridgeSubsystem::WriteInstanceRegistry`, called after a successful `Start` (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:132`).
- Location: `FPaths::ProjectDir()/.soft-ue-bridge/instance.json`, with the directory created on demand (`:193-206`, `:216`). The header comment says `~/.soft-ue-bridge/instance.json` (`Source/SoftUEBridge/Public/Subsystem/SoftUEBridgeSubsystem.h:59`); the code uses the project directory.
- Contents: `{"port": <int>, "host": "127.0.0.1"}` (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:208-214`). No PID, instance ID, or token. The PID and `bridge_instance_id` appear only in the `GET /bridge` response.
- Deletion: `StopServer` deletes the file unconditionally, without checking which process wrote it (`:150-160`, `:228-236`). It runs on `Deinitialize` and `RestartServer`.
- Multiple instances of one project overwrite each other's file. When any of them stops, the file disappears while the others still run.
- `sessions.json` in the same directory survives shutdown (`Source/SoftUEBridge/Private/Session/BridgeSessionRegistry.cpp:532`, `:585`).
- Packaged builds: the same code runs, so the file lands under the packaged build's `ProjectDir()`, inside the staged output next to its `Content` folder.[^packaged] Write failure only logs an error (`Source/SoftUEBridge/Private/Subsystem/SoftUEBridgeSubsystem.cpp:201-205`, `:222-225`).

CLI side, resolution order in `get_server_url` (`soft_ue_cli/discovery.py:54-75`):

1. `--server URL` flag. `main` copies it into `SOFT_UE_BRIDGE_URL` before any call (`soft_ue_cli/__main__.py:5535-5543`, `:9760-9761`).
2. `SOFT_UE_BRIDGE_URL`, any scheme and host, trailing slash removed (`soft_ue_cli/discovery.py:63-64`).
3. `SOFT_UE_BRIDGE_PORT`, mapped to `http://127.0.0.1:<port>`. A non-integer value is skipped (`:66-70`).
4. The first `.soft-ue-bridge/instance.json` found walking up from the current directory. `host` and `port` are taken as written, defaulting to `127.0.0.1` and `8080`. A parse error yields no URL and does not continue the walk (`:10-28`, `:72-73`).
5. `http://127.0.0.1:8080` (`:75`).

Stale-port fallback: when `SOFT_UE_BRIDGE_PORT` is set, `SOFT_UE_BRIDGE_URL` is unset, and the request fails with a connection error, timeout, or HTTP error status, the client retries against the `instance.json` URL and prints a warning to stderr (`soft_ue_cli/discovery.py:31-51`; `soft_ue_cli/client.py:353-381`, `:448-476`).

`SOFT_UE_BRIDGE_PORT` is read by both the Unreal process and the CLI. The CLI has no `--port` or `--token` flag and reads no `SOFT_UE_BRIDGE_TOKEN`. The README documents a remote URL example, `http://192.168.1.10:8080` (`README.md:935`).

### Current versus target

| Area | Current | Target (#33 to #39) |
| --- | --- | --- |
| Bind address | Engine `[HTTPServer.Listeners]` setting, loopback when unset. Plugin's `127.0.0.1` drives only the port probe and logs. Non-loopback config goes unnoticed. | Loopback by default. Non-loopback only through explicit opt-in, with a warning. |
| Bind failure | Reported as running; `instance.json` still written. | Not specified. |
| Port | `SOFT_UE_BRIDGE_PORT` or 8080, then +1 up to 10 tries. | No change specified. |
| Token | None. | Per-instance token, generated at start, stored in `instance.json`. |
| Request check | None. CORS `*` on every response. | Token checked on every request. |
| Rejection | No `unauthorized` code; only standard JSON-RPC codes. | Structured `unauthorized` error. |
| CLI credential | None sent. | Read from `instance.json` automatically; `SOFT_UE_BRIDGE_TOKEN` and `--token` override. |
| `instance.json` | `port` and `host` only; last writer wins; any stop deletes it. | Also carries the token. |
| Packaged builds | File written to the packaged `ProjectDir()`; CLI finds it only when run from inside that tree. | Token written to a discoverable location. |
| Health `GET` | Unauthenticated; returns PID, instance ID, tool names, module paths. | Not specified. |

[^engine]: Engine citations come from the local UE 5.7.4 source at `C:\Program Files\Epic Games\UE_5.7\Engine`. They are verified there and do not live in this repository. Other engine versions may differ.

[^tick]: Speculation. `FHttpServerModule::Tick` exists at the cited lines, but the ticker registration that places it on the game thread was not traced.

[^subsystem]: Speculation. Engine subsystems initialize during `UEngine::Init` in both editor and game processes. This is standard engine behavior that was not traced in source for this document.

[^packaged]: Speculation. In a packaged build `FPaths::ProjectDir()` usually resolves to the staged `<ProjectName>/` folder relative to the binaries. Whether that folder is writable depends on where the build is installed. Neither was tested.
