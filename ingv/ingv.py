#
# apologies for the camelCase - the previous version was in JS

import dateutil.parser
import datetime
from datetime import date, time
from time import sleep

import http.client

import xml.etree.ElementTree as ET

class INGV():
    def __init__(self):
        self.domain = "webservices.ingv.it"
        self.endpoint = "/fdsnws/event/1/query"

        # service dates are UTC, though it shouldn't matter here
        self.startDate = dateutil.parser.parse("2010-01-01T00:00:00Z")
        self.endDate = dateutil.parser.parse("2010-02-10T00:00:00Z")

        self.windowHours = 6
        self.windows_per_file = 4*10 # //////////////////////////////2 months
        self.window_count = 0

        self.data_dir = "./csv_data/"
        self.sample_domain = "webservices.ingv.it"
        self.sample_path = "/fdsnws/event/1/query?starttime=2010-01-01T00:00:00&endtime=2010-01-01T06:00:00"

        self.csv = ""

    def main(self):
        startWindow = self.startDate
        # step through dates
        while(startWindow < self.endDate):
            startString = datetime.datetime.isoformat(startWindow)
            # crop to 2010-01-01T00:00:00 - timespec=seconds didn't work for me
            startString = startString[0:19]

            startWindow = startWindow + datetime.timedelta(hours=self.windowHours)
            endString = datetime.datetime.isoformat(startWindow)
            endString = startString[0:19]
            query = '?starttime=' + startString + '&endtime=' + endString
            path = self.endpoint + query
            sleep(1) # don't hammer the service
            xml = self.get_xml(self.domain, path)
            self.csv = self.csv + self.extract_data(xml)
            window_count = window_count + 1
            if(window_count == windows_per_file):
                window_count = 0
                filename = data_dir+"ingv_"+startString+".csv"
                print("Saving "+filename)
                self.save_csv(filename,csv)
                self.csv = ""


                # using low-level version in case of connection issues
    def get_xml(self, domain, path):
        connection = http.client.HTTPConnection(domain, timeout=5)
        connection.request('GET', path)
        response = connection.getresponse()
        # print('{} {} - a response on a GET request by using "http.client"'.format(response.status, response.reason))
        content = response.read().decode('utf-8')
        return content

        # xmlns="http://quakeml.org/xmlns/bed/1.2"
        # xmlns:q="http://quakeml.org/xmlns/quakeml/1.2"
        #
        # q:quakeml/eventParameters/event/origin/time/value
        # q:quakeml/eventParameters/event/origin/latitude/value
        # q:quakeml/eventParameters/event/origin/longitude/value
        # q:quakeml/eventParameters/event/origin/depth/value
        # q:quakeml/eventParameters/event/magnitude/mag/value

    ns = "{http://quakeml.org/xmlns/bed/1.2}"

    def extract_data(self,xml):
        print(xml+"\n\n\n")
        root = ET.fromstring(xml)
        eventParameters = root.find(ns+'eventParameters')
        current = ""
        for event in eventParameters.findall(ns+'event'):
            origin_element = event.find(ns+'origin')
            time = origin_element.find(ns+'time').find(ns+'value').text
            latitude = origin_element.find(ns+'latitude').find(ns+'value').text
            longitude = origin_element.find(ns+'longitude').find(ns+'value').text
            depth = origin_element.find(ns+'depth').find(ns+'value').text

            magnitude_element = event.find(ns+'magnitude')
            magnitude = magnitude_element.find(ns+'mag').find(ns+'value').text

            current = current + time+", "+latitude+", "+longitude+", "+depth+", "+magnitude+"\n"
            return current

    def save_csv(filename, csv):
        with open(filename, 'w') as file:
            file.write(csv)

if __name__ == "__main__":
    INGV().main()
