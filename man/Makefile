# Use the remark-man node-js package to generate the documentation off the
# GitHub markdown file.
# See https://github.com/remarkjs/remark-man.
noah.1: noah.md
	node noah.js

noah.ps: noah.1
	groff -tmandoc noah.1 > noah.ps

clean:
	rm -f noah.ps
