import http.client
import re

scheme = "http"
host = "78.46.38.217"
port = 80
path = "/vlf15"

chunk_size = 100
# sync_magic = bytes("OggS", 'utf-8')

# url = "78.46.38.217:80/vlf15"
connection = http.client.HTTPConnection(host, port)
connection.request("GET", path)
response = connection.getresponse()
print(response.status, response.reason)

# data1 = r1.read()  # This will return entire content.
# The following example demonstrates reading data in chunks.
connection.request("GET", path)
response = connection.getresponse()

sync_re = re.compile(b"OggS") # magic string between Ogg pages

init_state = 0 # before the first OggS marker
accumulating_state = 1
end_state = 2

state = init_state
page = bytes(0)
chunks = bytes(0)

class OggPage:
    def __init__(self):
        self.data = bytes(0) # empty bytes

    #    if self.sync_re.match(page_start)
#        self.page_start = page_start


    def append(self, chunk):
        self.data = self.data + chunk

    def parse(self):
        self.page_type = self.page[5]
#  The value 1 specifies that the page contains data of a packet continued from the previous page. The value 2 specifies that this is the first page of the stream, and the value 4 specifies that this is the last page of the stream. These values can be combined with addition or logical OR.
        self.granule_position = self.page[6:14]
        self.serial_number = self.page[7:18]
        self.sequence_number = self.page[19:25]
        self.checksum = self.page[23:27]
        self.n_segments = self.page[28]

    def __str__(self):
        return str(self.data)

def handle_page(page):
    print("PAGE")
    print(len(page.data))
    print(page)

# print(response.read(10000), end='', flush=True) # .decode("ISO-8859-1")
current_page = OggPage()
current_data = bytes(0)

while not response.closed:
    chunk = response.read(chunk_size)
    chunks = chunks + chunk
    search = sync_re.search(chunks) # check if there is an Oggs marker
    if search == None:
        continue

    # add tail end of previous
    current_page.append(chunks[0:search.start()-1])
    handle_page(current_page)
    current_page = OggPage()
    print("search.end() = "+str(search.end()))
    print("len(chunks) = "+str(len(chunks)))
    chunks = chunks[:search.end()]
    print("len(chunks) = "+str(len(chunks)))



    #
    # if search:
    #     if init_state:
    #         page = chunks[0 : search.start()-1]
    #         state = start_state
    #         continue
    #     if accumulating_state:
    #         print("PAGE")
    #         print(page) # .decode("ISO-8859-1")
    #         page = chunks[search.end()+1 : len(chunks)-1]
    #         chunks = bytes(0)




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
