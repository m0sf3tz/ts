create database checkin_co;

\connect checkin_co;

drop table timeinfo;
drop table employeeinfo;
drop table shiftinfo;
drop table registeredDeviceId;
drop table sitepasswords;

create table timeinfo(id bigint NOT NULL, 
	                    employee VARCHAR(50), 
                      datework date,
                      type varchar(50),
                      timestamp time without time zone
                 	    );

create table employeeinfo(id serial PRIMARY KEY,
                        employee VARCHAR(50) UNIQUE,
                        salary   integer,
                        email    varchar UNIQUE,
                        shift_m_s time without time zone,
                        shift_m_e time without time zone,
                        shift_t_s time without time zone,
                        shift_t_e time without time zone,
                        shift_w_s time without time zone,
                        shift_w_e time without time zone,
                        shift_th_s time without time zone,
                        shift_th_e time without time zone,
                        shift_f_s time without time zone,
                        shift_f_e time without time zone
                       );
                        
create table shiftinfo(userid int NOT NULL, 
	           employee VARCHAR(50), 
             datework date,
             workday varchar (50),
             expected_login time without time zone,
             expected_logout time without time zone
             );

create table registeredDeviceId(deviceId int NOT NULL
                 );

create table sitepasswords(username VARCHAR(50), 
                           password VARCHAR(50)
                          );


