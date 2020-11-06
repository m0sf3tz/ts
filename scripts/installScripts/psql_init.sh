# we need to do this cause aws by defualt does not have SQL
sudo sh -c 'echo "deb http://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list'

# Import the repository signing key:
wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | sudo apt-key add -

# Update the package lists:
sudo apt-get update

# Install the latest version of PostgreSQL.
# If you want a specific version, use 'postgresql-12' or similar instead of 'postgresql':
sudo apt-get install postgresql-12 -y

sudo -u postgres psql -f setup.sql

sudo chown postgres pg_hba.conf
sudo chgrp postgres pg_hba.conf 
sudo -u postgres cp pg_hba.conf /etc/postgresql/12/main

sudo systemctl restart postgresql.service
