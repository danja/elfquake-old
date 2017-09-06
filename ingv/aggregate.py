import glob
import csv
import numpy as np
from numpy import genfromtxt
import hickle as hkl

class Aggregate():
    def __init__(self):
        self.csv_dir = "./csv_data/raw/"

    def main(self):
        got_start_date = False
        pattern = self.csv_dir + "*.csv"
        for filename in glob.glob(pattern):
            print(filename+"------------------------")
# 2007-01-02T05:28:38.870000, 43.612, 12.493, 7700, 1.7
            with open(filename, newline='\n') as csvfile:
                reader = csv.reader(csvfile, delimiter=',')
                for row in reader:
                    #print('-'.join(row))
                    print(row)
                    date_string = row[0].strip()
                    datetime = np.datetime64(date_string)
                    if not got_start_date:
                        self.start_date = datetime
                        got_start_date = True
                    print(datetime)
                    print(self.start_date)
                    latitude = float(row[1].strip())
                    longitude = float(row[2].strip())
                    depth = float(row[3].strip())
                    magnitude = float(row[4].strip())
            # X = np.zeros((nt, n_depth, n_lat, n_long), np.float32)

            # read csv file and fill in X

            # hkl.dump(X, 'data.hkl')


if __name__ == "__main__":
    Aggregate().main()
