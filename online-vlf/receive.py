#### NOTE TO SELF -
#### TRY WITH SOME LOCAL ARTIFICIAL DATA

import re
import http.client

scheme = "http"
host = "78.46.38.217"
port = 80
path = "/vlf15"

# must be >28 because of headers
chunk_size = 100

# sync_magic = bytes("OggS", 'utf-8')

# url = "78.46.38.217:80/vlf15"
connection = http.client.HTTPConnection(host, port)
connection.request("GET", path)
response = connection.getresponse()
print(response.status, response.reason)
print("\n\n\n\n")
# data1 = r1.read()  # This will return entire content.
# The following example demonstrates reading data in chunks.
connection.request("GET", path)
response = connection.getresponse()

chunks = b""

count = 0
first = True

while not response.closed and count < 10 :
    count = count + 1
    chunk = response.read(chunk_size)
    chunks = chunks + chunk

print(list(chunks))
