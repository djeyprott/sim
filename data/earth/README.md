# Earth relief

`elevation_l7.bin`: Earth's surface height on the level-7 grid (163,842
cells), one little-endian int16 per cell in metres; land cells are at least
+1, sea cells at most -1. Built by `tools/bake_earth.cpp` (target
`histsim_bake_earth`) from ETOPO10's ice-surface grid:

    histsim_bake_earth etopo10_ice_g_i2.bin data/earth/elevation_l7.bin

Sources: ETOPO1 Global Relief Model (NOAA NCEI, doi:10.7289/V5C8276M,
public domain), resampled to 10 arc-minutes by ETOPO10
(https://github.com/g2e/etopo10, MIT License, Copyright (c) 2017 Garrett
Euler).
