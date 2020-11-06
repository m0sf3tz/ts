sudo -u postgres psql -c "DROP DATABASE checkin_co"
sudo -u postgres psql -c "CREATE DATABASE checkin_co"
sudo -u postgres psql checkin_co < db_dump.psql 
