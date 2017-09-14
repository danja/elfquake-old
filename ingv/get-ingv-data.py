#
# apologies for the camelCase - the previous version was in JS

import dateutil.parser
import datetime
from datetime import date, time
from time import sleep

import http.client

import xml.etree.ElementTree as ET

        ## ''Aquila 6 April 2009 > 5 mag
        # 42.3476°N 13.3800°ECoordinates: 42.3476°N 13.3800°E[1]

        # http://webservices.ingv.it/fdsnws/event/1/query?starttime=2009-04-01T00:00:00&endtime=2009-04-10T00:00:00

# curl -i "http://webservices.ingv.it/fdsnws/event/1/query?starttime=2010-01-01T00:00:00&endtime=2010-01-01T06:00:00"

class INGV():
    def __init__(self):
        self.errors = ""
        self.domain = "webservices.ingv.it"
        self.endpoint = "/fdsnws/event/1/query"

        # service dates are UTC, though it shouldn't matter here

        # self.startDate = dateutil.parser.parse("2009-04-01T00:00:00Z") l'Aquila
        self.startDate = dateutil.parser.parse("1997-01-01T00:00:00Z")
        self.endDate = dateutil.parser.parse("2017-08-30T00:00:00Z")

        minlat = "40"
        maxlat = "47"
        minlon = "7"
        maxlon = "15"
#geographic-constraints=boundaries-rect
        self.region_string = "geographic-constraints=boundaries-rect&minlat="+minlat+"&maxlat="+maxlat+"&minlon="+minlon+"&maxlon="+maxlon

        self.windowHours = 1 #
        self.windows_per_file =  24*30 # 30 day blocks

        self.data_dir = "./csv_data/raw/"

        self.sample_domain = "webservices.ingv.it"
        self.sample_path = "/fdsnws/event/1/query?starttime=2010-01-01T00:00:00&endtime=2010-01-01T06:00:00"

        self.pause = 0.1 # delay between GETs to be kinder to the service
        self.timeout = 300 # for the GET 5 mins - sometimes it takes a long time

        self.csv = ""

    def main(self):
        window_count = 0
        startWindow = self.startDate
        # step through dates
        while(startWindow < self.endDate):
            startString = datetime.datetime.isoformat(startWindow)
            # crop to 2010-01-01T00:00:00 - timespec=seconds didn't work for me
            startString = startString[0:19]

            startWindow = startWindow + datetime.timedelta(hours=self.windowHours)
            endString = datetime.datetime.isoformat(startWindow)
            endString = endString[0:19]
            query = "?"+self.region_string
            query = query + '&starttime=' + startString + '&endtime=' + endString
            path = self.endpoint + query
            sleep(self.pause) # don't hammer the service

            try:
                xml = self.get_xml(self.domain, path)
            except ValueError as err:
                print(err)
                print()
            if xml != "":
                self.csv = self.csv + self.extract_data(xml,query)
            window_count = window_count + 1
            if(window_count == self.windows_per_file):
                window_count = 0
                filename = self.data_dir+"ingv_"+startString+".csv"
                print("Saving "+filename)
                self.save_text(filename,self.csv)
                self.csv = ""
        self.save_text(self.data_dir+"error.log", self.errors)

    # using low-level version to log connection issues
    def get_xml(self, domain, path):
        # print("PATH = "+path)
        connection = http.client.HTTPConnection(domain, timeout=self.timeout) # , timeout=self.timeout
        connection.request('GET', path)
        response = connection.getresponse()
        url = "http://"+domain+path
        print("Query: "+url+"\n")
        if(response.status == 204): # no content
            return ""
        if(response.status == 200):
            content = response.read().decode('utf-8')
        else:
            http_error = "HTTP "+str(response.status)+"  "+response.reason
            message = "Unexpected response from \n"+url+"\n"+http_error
            content = ""
            if response.status < 200 or response.status >= 300:
                self.errors = self.errors + message
                raise ValueError(http_error)
        return content

        # xmlns="http://quakeml.org/xmlns/bed/1.2"
        # xmlns:q="http://quakeml.org/xmlns/quakeml/1.2"
        #
        # q:quakeml/eventParameters/event/origin/time/value
        # q:quakeml/eventParameters/event/origin/latitude/value
        # q:quakeml/eventParameters/event/origin/longitude/value
        # q:quakeml/eventParameters/event/origin/depth/value
        # q:quakeml/eventParameters/event/magnitude/mag/value



    def extract_data(self,xml,query): # query is for debugging bad results
        ns = "{http://quakeml.org/xmlns/bed/1.2}"
        # print(xml+"\n\n\n")
        try:
            root = ET.fromstring(xml)
        except ET.ParseError as err:
            url = "http://"+self.domain+self.endpoint+query
            message = "Error in XML from \n"+url+"\n--------\n"+xml+"\n---------\n"+err.msg+"\n"
            self.errors = self.errors + message
            print(message)
            return ""

        eventParameters = root.find(ns+'eventParameters')
        current = ""
        count = 0
        for event in eventParameters.findall(ns+'event'):
            origin_element = event.find(ns+'origin')
            time = origin_element.find(ns+'time').find(ns+'value').text
            latitude = origin_element.find(ns+'latitude').find(ns+'value').text
            longitude = origin_element.find(ns+'longitude').find(ns+'value').text
            depth = origin_element.find(ns+'depth').find(ns+'value').text

            magnitude_element = event.find(ns+'magnitude')
            magnitude = magnitude_element.find(ns+'mag').find(ns+'value').text

            count = count + 1
            current = current + time+", "+latitude+", "+longitude+", "+depth+", "+magnitude+"\n"
        print("\nEvents from query = "+str(count)+"\n")
        return current

    def save_text(self, filename, text):
        with open(filename, 'w') as file:
            file.write(text)

if __name__ == "__main__":
    INGV().main()
