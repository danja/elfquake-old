import glob
import csv
import numpy as np
from numpy import genfromtxt
import hickle as hkl

class Aggregate():
    def __init__(self):
        self.csv_dir = "./csv_data/raw/"
        # 40N-47N, 7E-15E - northern Italy

        ## ''Aquila 6 April 2009 > 5 mag
        # 42.3476°N 13.3800°ECoordinates: 42.3476°N 13.3800°E[1]

        # http://webservices.ingv.it/fdsnws/event/1/query?starttime=2009-04-01T00:00:00&endtime=2009-04-10T00:00:00

        # hmm, note magnitude 6.1 in Emilia-Romagna 2012
        # 44.9°N 11.24°E
        self.min_latitude = 40
        self.max_latitude = 47
        self.min_longitude = 7
        self.max_longitude = 15

    def main(self):
        count = self.count_records() # hopelessly inefficient, but who cares, data is smallish
        print("COUNT = "+str(count))
        X = self.load_records(count)

            # 2007-01-02T05:28:38.870000, 43.612, 12.493, 7700, 1.7

# 2007-01-02T05:28:38.870000, 43.612, 12.493, 7700, 1.7
    def load_records(self, count):
        got_start_date = False
        in_zone_count = 0
        max_depth = 0
        max_magnitude = 0

        X = np.zeros((count, 128, 128), np.float32) # , n_depth, n_mag

        pattern = self.csv_dir + "*.csv"

        for filename in glob.glob(pattern):

            with open(filename, newline='\n') as csvfile:
                 reader = csv.reader(csvfile, delimiter=',')
                 for row in reader:
                    # print(row)
                    date_string = row[0].strip()
                    datetime = np.datetime64(date_string)
                    if not got_start_date:
                        self.start_date = datetime
                        got_start_date = True
#                    print(datetime)

                    latitude = float(row[1].strip())
                    longitude = float(row[2].strip())
                    if not self.in_zone(latitude, longitude):
                        continue

                    in_zone_count = in_zone_count + 1
                    depth = float(row[3].strip())
                    magnitude = float(row[4].strip())
                    if magnitude > 4:
                        print(row)
                    if depth > max_depth:
                        max_depth = depth
                    if magnitude > max_magnitude:
                        max_magnitude = magnitude

        print("in_zone_count = "+str(in_zone_count))
        print("max_depth = "+str(max_depth))
        print("max_magnitude = "+str(max_magnitude))
        return X

            #        latitude = scale_lat(latitude)
            #        longitude = scale_lat(longitude)

            # read csv file and fill in X

            # hkl.dump(X, 'data.hkl')

    def count_records(self):
        count = 0

        pattern = self.csv_dir + "*.csv"

        for filename in glob.glob(pattern):
            with open(filename, newline='\n') as csvfile:
                reader = csv.reader(csvfile, delimiter=',')
                for row in reader:
                    count = count + 1
                    depth = float(row[3].strip())
                    magnitude = float(row[4].strip())

# 2007-01-02T05:28:38.870000, 43.612, 12.493, 7700, 1.7
        return count
    # is the point within the region of interest?
    # 40N-47N, 7E-15E - northern Italy
    def in_zone(self, latitude, longitude):
        if latitude < self.min_latitude or latitude >= self.max_latitude:
            return False
        if longitude < self.min_longitude or longitude >= self.max_longitude:
            return False
        return True

if __name__ == "__main__":
    Aggregate().main()
