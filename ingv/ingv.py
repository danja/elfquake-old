import dateutil.parser
import datetime
from datetime import date, time

import http.client

domain = "webservices.ingv.it"
endpoint = "/fdsnws/event/1/query"

# service dates are UTC, though it shouldn't matter here
startDate = dateutil.parser.parse("2010-01-01T00:00:00Z")
endDate = dateutil.parser.parse("2010-02-10T00:00:00Z")

windowHours = 6

# step through dates
while (startDate < endDate):

  startString = datetime.datetime.isoformat(startDate)
  # crop to 2010-01-01T00:00:00 - timespec=seconds didn't work for me
  startString = startString[0:19]

  startDate = startDate + datetime.timedelta(hours=windowHours)
  endString = datetime.datetime.isoformat(startDate)
  endString = startString[0:19]
  query = '?starttime=' + startString + '&endtime=' + endString
  path = endpoint + query
  print(path)

# print(get_xml(domain, path))

# using low-level version in case of connection issues
def get_xml(domain, path):
    connection = http.client.HTTPConnection(domain, timeout=5)
    connection.request('GET', path)
    response = connection.getresponse()
    print('{} {} - a response on a GET request by using "http.client"'.format(response.status, response.reason))
    content = response.read().decode('utf-8')
    return content
