import glob
import csv
import numpy as np
from numpy import genfromtxt
import hickle as hkl

class Aggregate():
    def __init__(self):
        self.csv_dir = "./csv_data/raw/"
        #

    def main(self):
        got_start_date = False
        pattern = self.csv_dir + "*.csv"
        count = 0
        max_depth = 0
        max_magnitude = 0

# 2007-01-02T05:28:38.870000, 43.612, 12.493, 7700, 1.7

        # count records - hopelessly inefficient, but who cares
        for filename in glob.glob(pattern):
            with open(filename, newline='\n') as csvfile:
                reader = csv.reader(csvfile, delimiter=',')
                for row in reader:
                    count = count + 1
                    depth = float(row[3].strip())
                    magnitude = float(row[4].strip())
                    if depth > max_depth:
                        max_depth = depth
                    if magnitude > max_magnitude:
                        max_magnitude = magnitude

# 2007-01-02T05:28:38.870000, 43.612, 12.493, 7700, 1.7

        print("COUNT = "+str(count))
        print("max_depth = "+str(max_depth))
        print("max_magnitude = "+str(max_magnitude))

        self.X = np.zeros((count, 128, 128), np.float32) # , n_depth, n_mag
        for filename in glob.glob(pattern):
            # print(filename+"------------------------")

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
                    depth = float(row[3].strip())
                    magnitude = float(row[4].strip())

            #        latitude = scale_lat(latitude)
            #        longitude = scale_lat(longitude)

            # read csv file and fill in X

            # hkl.dump(X, 'data.hkl')


if __name__ == "__main__":
    Aggregate().main()
