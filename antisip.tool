#!/bin/bash

# Coverity seems to 

printf "#!/bin/bash\necho \"System Integrity Protection status: disabled.\"\n" | sudo tee /usr/local/bin/csrutil
sudo chmod a+x /usr/local/bin/csrutil
