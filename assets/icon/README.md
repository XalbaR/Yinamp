# Yinamp application icon

Original placeholder-compatible Yinamp artwork: a gold angular Y on a slate Classic
panel with a silver edge and transparent outer background. It is not a Windows default
icon or a copied Winamp logo.

`Yinamp.svg` is the editable source. `generate_icon.py` uses only Python's standard
library to create the checked-in RGBA `Yinamp.png` preview and `Yinamp.ico` with
16, 20, 24, 32, 40, 48, 64 and 256 pixel frames. Run `python assets/icon/generate_icon.py`
from the repository to regenerate; this is an optional authoring step, not a build or
runtime dependency. The native RC compiler directly embeds the checked-in ICO.
