var parseString = require('xml2js').parseString;

const url = 'http://cnt.rm.ingv.it/feed/atom/all_week';

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
  console.log(JSON.stringify(result, null, 2));
};
