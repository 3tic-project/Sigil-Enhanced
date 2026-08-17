# Split Editor Groups

The editor area can split into two vertical groups so two different EPUB resources stay visible at once (typically XHTML above CSS).

## Open

`View → Editor Layout → Split Editor Down`

This only creates an empty lower group. It does not clone the current file.

Open another file from Book Browser into the **active** group (still the upper group until “Open in Other Editor Group” lands). Join with `Join Editor Groups` — tabs move; they are not reloaded.

## Rules

- One Resource still has one editor.
- The upper group cannot close its last tab.
- Closing the last tab in the lower group leaves the empty group in place.
- `Close Other Tabs` is still window-wide: every tab except the current one, in both groups.
- Restart does not yet remember the split; that comes with layout persistence.

Split Editor Down is not `Edit → Split At Cursor`, which still splits an XHTML file into two files.
