var parseString = require('xml2js').parseString;

const url = 'http://cnt.rm.ingv.it/feed/atom/all_week';

// Mozzanella
const homeLat = 44.1490514;
const homeLong = 10.3860636;

/* Dependency-less GET promise from
   https://www.tomas-dvorak.cz/posts/nodejs-request-without-dependencies/ */
const getContent = function(url) {
  // return new pending promise
  return new Promise((resolve, reject) => {
    // select http or https module, depending on reqested url
    const lib = url.startsWith('https') ? require('https') : require('http');
    const request = lib.get(url, (response) => {
      // handle http errors
      if (response.statusCode < 200 || response.statusCode > 299) {
         reject(new Error('Failed to load page, status code: ' + response.statusCode));
       }
      // temporary data holder
      const body = [];
      // on every content chunk, push it to the data array
      response.on('data', (chunk) => body.push(chunk));
      // we are done, resolve promise with those joined chunks
      response.on('end', () => resolve(body.join('')));
    });
    // handle connection errors of the request
    request.on('error', (err) => reject(err))
    })
};



getContent(url)
  .then((xml) =>
  parseString(xml, function (err, result) {
    process(result);
})
)
  .catch((err) => console.error(err));

// JSON.stringify(result, null, 2)

var process = function(data) {
  console.log(JSON.stringify(data, null, 2));
};

/* Distance between two points using haversine formula */

var R = 6371; // avg radius of Earth, km

var distance = function(lat1, lon1, lat2, lon2){
var x1 = lat2-lat1;
var dLat = toRad(x1);
var x2 = lon2-lon1;
var dLon = toRad(x2);
var a = Math.sin(dLat/2) * Math.sin(dLat/2) +
                Math.cos(toRad(lat1)) * Math.cos(toRad(lat2)) *
                Math.sin(dLon/2) * Math.sin(dLon/2);
var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
return R * c;
};

var toRad = function(x) {
   return x* Math.PI / 180;
};

// console.log(distance(lat1, lon1, lat2, lon2));
