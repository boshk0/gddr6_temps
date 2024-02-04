echo '#!/bin/bash' > ~/sudo_gdb.sh
echo 'sudo /usr/bin/gdb "$@"' >> ~/sudo_gdb.sh
chmod +x ~/sudo_gdb.sh
