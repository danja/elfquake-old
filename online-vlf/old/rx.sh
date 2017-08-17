vtvorbis -dn 78.46.38.217,4415 @vlf15   # Cumiana

vtcat -E30 @vlf15 | vtstat -h bins=100 > out.txt

vtcat -E30 @vlf15 raw

vtread  raw |
      vtfilter -ath=6 -g4 -- -:1 |
         vtresample -q2 -r30000 |
            vtcat -E60 |
               vtsgram -p200 -b300 -s '-z60 -Z-30' > img840.png
