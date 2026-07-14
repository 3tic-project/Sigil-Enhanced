# Legacy Python Plugin System (API v1)

This document describes the Python plugin implementation that predates the live
Plugin API. It is a source-derived compatibility contract for the v2 work. It
documents behavior in the current tree, not an idealized interface.

## Scope and source map

The v1 system is implemented by these components:

| Component | Responsibility |
| --- | --- |
| `Misc/PluginDB.*` | Discover, validate, install, remove, and look up plugins. |
| `Misc/Plugin.*` | Hold metadata parsed from `plugin.xml`. |
| `MainUI/MainWindow.cpp` | Populate plugin menus and invoke `PluginRunner`. |
| `Dialogs/PluginRunner.*` | Synchronize the book, launch Python, display output, validate results, and import changes. |
| `plugin_launchers/python/launcher.py` | Select a container, import `plugin.py`, invoke `run()`, and serialize result XML. |
| `plugin_launchers/python/wrapper.py` | Parse the OPF, provide a staged filesystem view, and record changes. |
| `plugin_launchers/python/*container.py` | Public Python API for edit, input, output, and validation plugins. |

The authoritative public method inventory is generated from
`bookcontainer.py`, `inputcontainer.py`, `outputcontainer.py`, and
`validationcontainer.py`. Private names and imported helper classes are not part
of the container API.

## Discovery and metadata

Plugins are directories below `<preferences>/plugins`. A valid plugin has this
layout:

```text
<preferences>/plugins/<directory>/
|-- plugin.xml
`-- plugin.py
```

`PluginDB` parses these `plugin.xml` elements:

| Element | Required by validation | Meaning |
| --- | --- | --- |
| `name` | yes | Display name and lookup key. |
| `type` | yes | `edit`, `input`, `output`, or `validation`. |
| `engine` | yes | Currently a supported historical engine name, normally `python3.4`. Multiple elements are comma-joined. |
| `author` | no | Display metadata. |
| `description` | no | Plugin table tooltip. |
| `version` | no | Display metadata. |
| `oslist` | no | Comma-separated `osx`, `win`, and/or `unx`. |
| `autostart` | no | Start the process without waiting for the Run button. Defaults to `false`. |
| `autoclose` | no | Close the runner after success. Defaults to `false`. |

Unknown XML elements are ignored. The optional icon is resolved independently
from `plugin.svg` or `plugin.png`, preferring the plugin preference directory
and SVG. The manager stores the Python interpreter path, bundled-interpreter
choice, and ten quick-launch assignments in `SettingsStore`.

## Host lifecycle

`MainWindow::runPlugin()` constructs a stack-scoped `PluginRunner` and calls its
modal `exec()` method. The runner then:

1. Resolves the plugin and Python interpreter.
2. Creates a private temporary output directory.
3. Writes the line-oriented `sigil.cfg` compatibility file.
4. Calls `MainWindow::SaveTabData()`.
5. Suspends the resource watcher, calls `Book::SaveAllResourcesToDisk()`, and
   resumes the watcher.
6. Launches `launcher.py ebook_root output_dir type plugin.py` with `QProcess`.
7. Treats stdout as both user output and the final result XML channel; stderr is
   appended to the runner console.
8. On a successful result, validates and imports staged changes into the live
   `Book`, refreshes affected UI, and marks the book modified.

The modal runner permits a nested Qt event loop, but it does not provide a
persistent session. A plugin sees the book state captured before process start.
It cannot observe later cursor, selection, tab, or document changes.

### `sigil.cfg`

The temporary configuration is positional rather than self-describing. It
contains the OPF book path, application and preference paths, Hunspell paths,
UI and spellcheck languages, dirty/file state, theme colors and font,
Automate state/parameter, font obfuscation data, and the Book Browser selection.
Changing its ordering is therefore a v1 protocol break.

### Python launcher

The launcher parses the current OPF into `Wrapper`, selects the public container
for the plugin type, imports the plugin module, and invokes:

```python
def run(container):
    return 0
```

An exit code of zero is success. For edit plugins, success causes the wrapper to
write a final staged OPF. Exceptions and nonzero returns are failures. Plugin
stdout and stderr are captured, escaped, and included in the final wrapper XML.

## Staging and result import

The wrapper maintains in-memory manifest, spine, guide, bindings, metadata, and
package-tag state. Content writes go to the output directory. Reads use the
output copy after a resource has been modified and the original book directory
otherwise. It records `added`, `deleted`, and `modified` entries.

The final XML contains status, messages, changed files, and optional validation
results. `PluginRunner` deliberately finds the last XML declaration in stdout,
then imports additions, deletions, and modifications. OPF and NCX changes are
ordered last where required.

Before applying results, the host:

- validates changed XHTML and offers a cancel path for malformed content;
- normalizes changed XML through `CleanSource`;
- rejects removal of the last XHTML resource;
- protects the current OPF and the EPUB 3 navigation document;
- closes tabs for deleted resources and ensures an XHTML tab remains open;
- suspends resource watching during import;
- batches resource removal where supported;
- publishes validation results to the Validation Results view;
- refreshes Book Browser, tabs, Preview-related caches, and modified state.

Cancellation first terminates the process, waits two seconds, then kills it and
waits again. Since staged data is outside the live `Book`, a failure, crash, or
cancel before import normally leaves the book unchanged.

## Edit `BookContainer` API

The edit container has 76 public members. Of these, `sigil_ui_lang` and
`sigil_spellcheck_lang` are accessed as properties; the other 74 are methods.
Unless stated otherwise, strings are Python `str` values and content methods
accept or return bytes according to the existing wrapper behavior.

### Environment and preferences

| API | Contract |
| --- | --- |
| `getPrefs()` / `savePrefs(prefs)` | Read or persist the plugin's `JSONPrefs`. |
| `launcher_version()` | Return the launcher API version. |
| `epub_version()` | Return the parsed EPUB package version. |
| `epub_is_standard()` | Report whether the EPUB uses Sigil's standard layout. |
| `sigil_ui_lang` | UI language property, defaulting to `en`. |
| `sigil_spellcheck_lang` | Spellcheck language property, defaulting to `en_US`. |
| `get_hunspell_library_path()` | Return the configured Hunspell library path. |
| `get_dictionary_dirs()` | Return available dictionary directories. |
| `get_epub_is_modified()` | Return the launch-time dirty flag. |
| `get_epub_filepath()` | Return the launch-time EPUB path or an empty string. |
| `colorMode()` / `color(role)` | Return `light`/`dark` and a supported UI role color. |
| `using_automate()` / `automate_parameter()` | Return launch-time Automate context. |

### Package structure

| API | Contract |
| --- | --- |
| `gettocid()` / `getpagemapid()` / `getnavid()` | Return special manifest IDs or `None`. |
| `getspine()` / `setspine(value)` | Get/set ordered `(idref, linear)` tuples. |
| `getspine_epub3()` / `setspine_epub3(value)` | Get/set `(idref, linear, properties)` tuples. |
| `spine_insert_before(pos, id, linear, properties=None)` | Insert a spine item. |
| `getspine_ppd()` / `setspine_ppd(value)` | Get/set package page progression direction. |
| `setspine_idref_epub3_attributes(id, linear, properties)` | Update one spine itemref. |
| `getguide()` / `setguide(value)` | Get/set `(type, title, href)` tuples. |
| `getbindings_epub3()` / `setbindings_epub3(value)` | Get/set `(media_type, handler_id)` tuples. |
| `getmetadataxml()` / `setmetadataxml(xml)` | Get/set the metadata XML fragment. |
| `getpackagetag()` / `setpackagetag(xml)` | Get/set the package start tag. |
| `get_opf()` | Serialize the wrapper's current staged package state. |

### Manifested and unmanifested content

| API | Contract |
| --- | --- |
| `readfile(id)` / `writefile(id, data)` | Read or stage replacement of a manifest item. |
| `addfile(id, basename, data, mime=None, properties=None, fallback=None, overlay=None)` | Add a manifest item using standard folder grouping. |
| `addbookpath(id, bookpath, data, mime=None)` | Add a manifest item at an explicit canonical book path. |
| `deletefile(id)` | Delete a manifest item and related spine entries. |
| `set_manifest_epub3_attributes(id, properties=None, fallback=None, overlay=None)` | Update EPUB 3 manifest attributes. |
| `readotherfile(path)` / `writeotherfile(path, data)` | Read or stage replacement of an unmanifested file. |
| `addotherfile(path, data)` / `deleteotherfile(path)` | Add or delete an unmanifested file. |
| `copy_book_contents_to(destdir)` | Materialize the wrapper's current full staged view. |

### Iterators

| API | Yield shape and order |
| --- | --- |
| `text_iter()` | `(id, href)` in spine order, then remaining XHTML. |
| `css_iter()` | `(id, href)` sorted by ID. |
| `image_iter()` / `font_iter()` / `media_iter()` | `(id, href, media_type)` sorted by ID. |
| `manifest_iter()` | `(id, href, media_type)` sorted by ID. |
| `manifest_epub3_iter()` | `(id, href, media_type, properties, fallback, overlay)` sorted by ID. |
| `spine_iter()` | `(idref, linear, href)` in spine order. |
| `spine_epub3_iter()` | `(idref, linear, properties, href)` in spine order. |
| `guide_iter()` | `(type, title, href, manifest_id)` in guide order. |
| `bindings_epub3_iter()` | `(media_type, handler_id, handler_href)` in bindings order. |
| `other_iter()` | Canonical book paths for unmanifested files. |
| `selected_iter()` | `('manifest', id)` or `('other', path)` using launch-time selection. |

### Mapping and path helpers

| API | Contract |
| --- | --- |
| `href_to_id`, `basename_to_id`, `bookpath_to_id` | Resolve to manifest ID, with optional fallback. |
| `id_to_href`, `id_to_bookpath`, `id_to_mime` | Resolve manifest fields, with optional fallback. |
| `id_to_properties`, `id_to_fallback`, `id_to_overlay` | Resolve EPUB 3 fields, with optional fallback. |
| `href_to_basename(href, fallback=None)` | Return the last href component. |
| `get_opfbookpath()` | Return the canonical package document book path. |
| `get_startingdir(bookpath)` | Return a book path's containing directory. |
| `build_bookpath(href, starting_dir)` | Resolve an href against a canonical book directory. |
| `get_relativepath(from_bookpath, to_bookpath)` | Build a relative href between resources. |
| `group_to_folders(group, fallback=None)` | Return configured folders for a resource group. |
| `mediatype_to_group(type, fallback=None)` | Map media type to Sigil resource group. |
| `font_bookpath_to_preferred_obfuscation_algorithm(path, fallback=None)` | Return launch-time font obfuscation preference. |

## Container variants

`OutputContainer` exposes 60 public members and is a read-only-shaped subset of
the book API. It includes
package access, content reads, iterators, mappings, paths, preferences, theme,
Hunspell, launch-time file state, and Automate context, but no mutators.

`ValidationContainer` extends `OutputContainer` with `add_result()` and
`add_extended_result()`, which append structured info, warning, and error
results. Those results carry a book path, line number, character offset, and
message and are published by the host after the process finishes.

`InputContainer` has 12 public members and no current book model. It exposes
preferences, launcher/UI context, Hunspell, theme, Automate context, and
`addotherfile()` for returning a new EPUB or other input result through the
output directory.

## Compatibility invariants for API v2

The live implementation must preserve these v1 guarantees when a plugin is run
in compatibility mode:

1. Plugins without an explicit v2 selection always use `PluginRunner`.
2. Existing `plugin.py` entry points and container method signatures remain
   import-compatible.
3. Writes are staged and become visible only after a successful run/commit,
   unless a new explicit live/flush API is used.
4. Read-your-writes works for manifested and unmanifested files.
5. Iterator tuple shapes and ordering remain stable.
6. Package mutations serialize to an equivalent OPF.
7. Failed, cancelled, and crashed plugins do not import uncommitted changes.
8. Validation results and Automate completion semantics remain compatible.
9. XHTML/XML and structural safety checks are not weakened.
10. v2 live reads do not require `SaveAllResourcesToDisk()` and editor writes
    use revisions to reject stale changes.

The v2 compatibility test suite must account for every public method listed
above. A method may be delegated to an unchanged local helper (for example path
calculation or preferences), but it may not silently disappear.

## Known v1 boundaries

- The host and plugin communicate control data through mixed stdout/XML.
- The positional `sigil.cfg` format has no version negotiation.
- The plugin reads a launch-time disk snapshot, not continuously live content.
- Selection and theme/configuration values are launch-time snapshots.
- The modal runner prevents natural long-lived interaction with the editor.
- File-level result XML has no patch, revision, transaction, event, progress, or
  structured cancellation semantics.
- Python plugins run with the user's operating-system permissions. Neither v1
  nor the planned RPC permission model is an OS sandbox.
