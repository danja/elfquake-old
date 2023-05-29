
vtcat -E30 @vlf15 | vtsgram -p200 -b100 -s '-z60 -Z-30' > 2017-07-02.png

https://linux.die.net/man/1/sox

vtcat -E30 @vlf15 | vtsgram -p200 -b128 -s '-m -l -a -r -X 1' > 2017-07-02.png

this works for 1/2 hr greyscale
vtcat -E1800 @vlf15 | vtsgram -p200 -b128 -s '-m -l -a -r -X 1' > 2017-07-02.png

sox FAIL spectrogram: usage: [options]
	-x num	X-axis size in pixels; default derived or 800
	-X num	X-axis pixels/second; default derived or 100
	-y num	Y-axis size in pixels (per channel); slow if not 1 + 2^n
	-Y num	Y-height total (i.e. not per channel); default 550
	-z num	Z-axis range in dB; default 120
	-Z num	Z-axis maximum in dBFS; default 0
	-q num	Z-axis quantisation (0 - 249); default 249
	-w name	Window: Hann (default), Hamming, Bartlett, Rectangular, Kaiser
	-W num	Window adjust parameter (-10 - 10); applies only to Kaiser
	-s	Slack overlap of windows
	-a	Suppress axis lines
	-r	Raw spectrogram; no axes or legends
	-l	Light background
	-m	Monochrome
	-h	High colour
	-p num	Permute colours (1 - 6); default 1
	-A	Alternative, inferior, fixed colour-set (for compatibility only)
	-t text	Title text
	-c text	Comment text
	-o text	Output file name; default `spectrogram.png'
	-d time	Audio duration to fit to X-axis; e.g. 1:00, 48
	-S time	Start the spectrogram at the given time through the input
