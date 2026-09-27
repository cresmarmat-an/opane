# Finding files

Every file opane loads (images, fonts, sounds, music, and material shaders) is
found by name through an ordered list of folders, called asset roots. That lets
you write short names such as `"click.wav"` instead of full paths.

A relative name is looked for in this order:

1. the roots you add, most recent first;
2. `./`, `./assets/`, and `./assets/images`, `sounds`, `music`, `fonts`,
   `shaders`, and `models` under the working directory;
3. the same folders beside the executable, so a program finds its files no
   matter which folder it was started from.

```cpp
opane::AddAssetRoot("C:/games/mygame/content");

opane::ResolveAssetPath("click.wav");   // the full path it resolves to, or ""
opane::GetAssetRoots();                 // the roots in search order
opane::ClearAssetRoots();               // back to the defaults
```

The first match wins, and the result is remembered, so later loads of the same
name do not search again. Adding or clearing roots forgets what was remembered.
Absolute paths are used as they are.

When a name matches nothing, the log lists every path that was tried, so you
can see where opane looked. What happens next depends on the loader: a missing
image draws as a magenta-and-black checkerboard, and a missing sound is silent.

## Limitations

- Files are read from the file system only. There is no support for reading
  from zip archives or packed asset files.
- Names are matched as the file system matches them, so on Linux they are case
  sensitive.
- Asset roots are shared by the whole process. ludifex has its own list,
  set with `ludifex::AddAssetRoot`.
