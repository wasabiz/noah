var vfile = require('to-vfile');
var unified = require('unified');
var markdown = require('remark-parse');
var man = require('remark-man');

unified()
  .use(markdown)
  .use(man)
  .process(vfile.readSync('noah.md'), function (err, file) {
    if (err) throw err;
    file.extname = '.1';
    vfile.writeSync(file);
  });
