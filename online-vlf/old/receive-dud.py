#
# import io
# import soundfile as sf
# from urllib.request import urlopen
#
# url = "http://tinyurl.com/shepard-risset"
# data, samplerate = sf.read(io.BytesIO(urlopen(url).read()))
#
#
#
#
# import soundfile as sf
# with open('filename.flac', 'rb') as f:
#     data, samplerate = sf.read(f)
#
#
#
#
# import soundfile as sf
#
# with sf.SoundFile('myfile.wav', 'rw') as f:
#     while f.tell() < len(f):
#         pos = f.tell()
#         data = f.read(1024)
#         f.seek(pos)
#         f.write(data*2)

import io
import soundfile as sf
from urllib.request import urlopen

scheme = "http"
host = "78.46.38.217"
port = 80
path = "/vlf15"

url = scheme+"://"+host+":"+str(port)+path
dat, samplerate = sf.read(io.BytesIO(urlopen(url).read(2048)))

with sf.SoundFile(dat) as f:
#    while f.tell() < len(f):
    while True:
        data = f.read(1024)
        print("DATA")
        print(data)
