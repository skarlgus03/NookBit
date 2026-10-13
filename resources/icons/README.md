# Application icon

`nookbit.png` is the approved original artwork. Keep its transparent background,
white rounded tile and original canvas; do not crop or redesign during conversion.
`nookbit.ico` contains 16, 24, 32, 48, 64, 128 and 256 px RGBA frames.

To replace the icon, run this from the project root with Python and Pillow:

```powershell
python tools/convert_app_icon.py "C:/path/to/approved-icon.png"
```

The converter checks the sizes and transparency. Rebuild and package the app:
the Qt resource supplies the application/window/tray icon, and `nookbit.rc.in`
embeds the same ICO in the Windows executable. Windows Explorer may keep an
old icon in its cache even after the executable has been updated.
