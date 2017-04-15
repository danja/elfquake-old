page_type = page[5]
#  The value 1 specifies that the page contains data of a packet continued from the previous page. The value 2 specifies that this is the first page of the stream, and the value 4 specifies that this is the last page of the stream. These values can be combined with addition or logical OR.

granule_position = page[6:14]

serial_number = page[7:18]

sequence_number = page[19:25]

checksum = page[23:27]

n_segments = page[28]
