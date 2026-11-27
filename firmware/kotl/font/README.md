# Console font

The console uses **Noto Sans Mono Regular**, 20 px, with 12×28 cells. Printable
ASCII retains uppercase, lowercase and punctuation. The renderer blends 8-bit
coverage against its background rather than enlarging a binary bitmap.

The bundled font is from the [Noto project](https://github.com/notofonts/noto-fonts/blob/main/hinted/ttf/NotoSansMono/NotoSansMono-Regular.ttf),
licensed under [SIL Open Font License 1.1](OFL.txt). Its original font name and
license are preserved. The bundled files make builds independent of the network.

SHA-256 of `NotoSansMono-Regular.ttf`:
`d9e2b23d19f8230be7146f409a52b1d23117e635e28f2e2892cf91b7382f325b`.

Regenerate the atlas from the repository root with Pillow installed:

```sh
python3 scripts/make-console-font.py
make kotl-efi
```

The generator renders at four times the final size and downsamples with Lanczos.
`ascii20.bin` holds 95 cells in character, row, column order (31,920 bytes).
It is committed as a build input; normal firmware builds do not require Pillow.
