import re
import requests
import sys

try:
    from StringIO import StringIO as BytesIO
except ImportError:
    from io import BytesIO

def icy_monitor(stream_url, callback=None):

    r = requests.get(stream_url, headers={'Icy-MetaData': '1'}, stream=True)
    if r.encoding is None:
        r.encoding = 'utf-8'

    byte_counter = 0
    meta_counter = 0
    metadata_buffer = BytesIO()
    print('Meta = ')
    print(r.headers)
#    metadata_size = int(r.headers['icy-metaint']) + 255

    data_is_meta = False


    for byte in r.iter_content(1):

        byte_counter += 1

        if (byte_counter <= 2048):
            pass

        if (byte_counter > 2048):
            if (meta_counter == 0):
                meta_counter += 1

            elif (meta_counter <= int(metadata_size + 1)):

                metadata_buffer.write(byte)
                meta_counter += 1
            else:
                data_is_meta = True

        if (byte_counter > 2048 + metadata_size):
            byte_counter = 0

        if data_is_meta:

            metadata_buffer.seek(0)

            meta = metadata_buffer.read().rstrip(b'\0')

            m = re.search(br"StreamTitle='([^']*)';", bytes(meta))
            if m:
                title = m.group(1).decode(r.encoding, errors='replace')
                print('New title: {}'.format(title))

                if callback:
                    callback(title)

            byte_counter = 0
            meta_counter = 0
            metadata_buffer = BytesIO()

            data_is_meta = False


def print_title(title):
    print('Title: {}'.format(title))



if __name__ == '__main__':

    stream_url = sys.argv[1]
    icy_monitor(stream_url, callback=print_title)