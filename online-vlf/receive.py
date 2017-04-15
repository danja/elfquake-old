import http.client
import re

scheme = "http"
host = "78.46.38.217"
port = 80
path = "/vlf15"

chunk_size = 10000
# sync_magic = bytes("OggS", 'utf-8')
sync_re = re.compile(b"OggS") # magic string between Ogg pages
# url = "78.46.38.217:80/vlf15"
connection = http.client.HTTPConnection(host, port)
connection.request("GET", path)
response = connection.getresponse()
print(response.status, response.reason)

# data1 = r1.read()  # This will return entire content.
# The following example demonstrates reading data in chunks.
connection.request("GET", path)
response = connection.getresponse()

init_state = 0 # before the first OggS marker
accumulating_state = 1
end_state = 2

state = init_state
page = bytes(0)
chunks = bytes(0)

# print(response.read(10000), end='', flush=True) # .decode("ISO-8859-1")

while not response.closed:
    chunks = chunks + response.read(chunk_size)
    search = sync_re.search(chunks)
    print(search.start())
    if search:
        if init_state:
            state = start_state
            continue
        if accumulating_state:
            page = chunks[0 : search.start()-1]
            print("PAGE")
            print(page) # .decode("ISO-8859-1")
            page = chunks[search.end()+1 : len(chunks)-1]
            chunks = bytes(0)


    #while state != end_state:
    #    chunk = response.read(chunk_size)
    #    match = chunk.match(sync_re)
    #    if match:
    #        if init_state:
    #            state = start_state
    #            break
    #        page = page + chunk()
            # m.start(), m.end()
        # Seek position and read N bytes
    # binary_file.seek(0)  # Go to beginning
    # couple_bytes = binary_file.read(2)

    # print(r1.read(200), end='', flush=True)  # 200 bytes
