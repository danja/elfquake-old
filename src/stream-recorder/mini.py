import miniaudio
import array
from time import sleep

buffer_chunks = []

def record_to_buffer():
    _ = yield
    while True:
        data = yield
        print(".", end="", flush=True)
        buffer_chunks.append(data)

def title_printer(client: miniaudio.IceCastClient, new_title: str) -> None:
    print("Stream title: ", new_title)

#def choose_device():
#    devices = miniaudio.Devices()
#    print("Available recording devices:")
#    captures = devices.get_captures()
#    for d in enumerate(captures):
#        print("{num} = {name}".format(num=d[0], name=d[1]['name']))
#    choice = int(input("record from which device? "))
#    return captures[choice]

with miniaudio.IceCastClient("http://5.9.106.210/vlf15",
        update_stream_title=title_printer) as source:
    print("Connected to internet stream, audio format:", source.audio_format.name)
    print("Station name: ", source.station_name)
    print("Station genre: ", source.station_genre)
    print("Press <enter> to quit playing.\n")

    # check https://pypi.org/project/miniaudio/

 #   stream = miniaudio.stream_any(source, source.audio_format)
    generator = miniaudio.stream_any(source, source.audio_format)
    #with miniaudio.PlaybackDevice() as device:
     #   device.start(stream)
      #  input()   # wait for user input, stream plays in background
#wav_write_file  (filename: str, sound: miniaudio.DecodedSoundFile) 
   # selected_device = choose_device()
   # capture = miniaudio.CaptureDevice(buffersize_msec=1000, sample_rate=44100, device_id=selected_device["id"])

    generator = record_to_buffer()
    print("Recording for 3 seconds")
    next(generator)
 
    sleep(3)
    # capture.stop()
   # stream.stop()

    buffer = b"".join(buffer_chunks)
    print("\nRecorded", len(buffer), "bytes")
    print("Wring to ./capture.wav")
    samples = array.array('h')
    samples.frombytes(buffer)
    sound = miniaudio.DecodedSoundFile('capture', 1, 22050, miniaudio.SampleFormat.SIGNED16, samples)
    #     sound = miniaudio.DecodedSoundFile('capture', capture.nchannels, capture.sample_rate, capture.format, samples)
    miniaudio.wav_write_file('capture.wav', samples)
    #miniaudio.wav_write_file('capture.wav', sound)
    print("Recording done")