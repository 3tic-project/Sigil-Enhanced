# Split Editor Groups

The editor area can split into two vertical groups so two different EPUB resources stay visible at once (typically XHTML above CSS).

## Open

`View → Editor Layout → Split Editor Down`

This only creates an empty lower group. It does not clone the current file.

Typical CSS workflow: right-click a stylesheet in Book Browser → **Open in Other Editor Group**. If the window is not split yet, Sigil splits first. A file that is already open is focused in its existing group, not cloned.

Tab context menu **Move Editor to Other Group** moves the same editor widget (undo/dirty/cursor stay). The last tab in the upper group cannot be moved.

**Open in Other Editor Group** target (Preferences → Enhanced):

- Inactive group (default): opens in the group that is not focused
- Always lower group
- Always upper group

Join with `Join Editor Groups` — tabs move; they are not reloaded.

## Rules

- One Resource still has one editor.
- The upper group cannot close its last tab.
- Closing the last tab in the lower group leaves the empty group in place.
- `Close Other Tabs` is still window-wide: every tab except the current one, in both groups.
- Restart restores whether the editor was split and the splitter ratio, not which files were open.
- Clicking into a group makes it active. Undo, Cut, and other edit commands follow the active group, even if that group's current tab did not change.

Focus the other group from `View → Editor Layout` (no default shortcut).

Split Editor Down is not `Edit → Split At Cursor`, which still splits an XHTML file into two files.
