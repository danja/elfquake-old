# ELFQuake

See [ELFQuake blog](https://elfquake.wordpress.com/)

## Status 2023-05-29

I'm returning to this after a few years absence.

Currently reorganising things a bit.  More material to follow.

### Pre-2023 :

Collect seismic data from INGV, train a PredNet network

ingv/get-ingv-data.py

- pulls INGV data, filters, dumps to CSV files (working)

ingv/aggregate.py

- filter/aggregate CSV data, dumps to HDF5 file (in progress)