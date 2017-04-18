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
        self.ogg_marker = self.data[0:4]
        self.page_type = self.data[5]
#  The value 1 specifies that the page contains data of a packet continued from the previous page. The value 2 specifies that this is the first page of the stream, and the value 4 specifies that this is the last page of the stream. These values can be combined with addition or logical OR.
        self.granule_position = self.data[6:14]
        self.serial_number = self.data[7:18]
        self.sequence_number = self.data[19:25]
        self.checksum = self.data[23:27]
        self.n_segments = self.data[28]

    def __str__(self):
        self.parse()
        string = "\nsize = " + str(len(self.data)) \
        + "\nogg_marker = " + str(self.ogg_marker) \
        + "\npage_type = " + str(self.page_type) \
        + "\ngranule_position =" + str(self.granule_position)  \
        + "\nserial_number = " + str(self.serial_number) \
        + "\nsequence_number = " + str(self.sequence_number) \
        + "\nself.checksum = " + str(self.checksum) \
        + "\nself.n_segments = " + str(self.n_segments) \
        + "\ndata =\n" + str(self.data)
        return string

def handle_page(page):
    print("PAGE")
    print(len(page.data))
    print(page)

# print(response.read(10000), end='', flush=True) # .decode("ISO-8859-1")
current_page = OggPage()
current_data = bytes(0)

# for checking
before_file = open("before_file",'wb')
before_size = 0
after_file = open("after_file",'wb')
after_size = 0

count = 0

while not response.closed and count < 100 :
    count = count + 1
    chunk = response.read(chunk_size)
    before_size = before_size + len(chunk)
    before_file.write(chunk)
    chunks = chunks + chunk
    search = sync_re.search(chunks) # check if there is an Oggs marker
    if search == None:
        continue

    # add tail end of previous
    current_page.append(chunks[0:search.start()-1])
    handle_page(current_page)
    after_size = after_size + len(current_page.data)
    after_file.write(current_page.data)
    current_page = OggPage()
    print("search.end() = "+str(search.end()))
    print("len(chunks) = "+str(len(chunks)))
    chunks = chunks[:search.end()]
    print("len(chunks) = "+str(len(chunks)))

before_file.close()
after_file.close()
print("before_size = "+str(before_size))
print("after_size = "+str(after_size))
