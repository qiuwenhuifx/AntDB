set grammar to oracle;
create table saverollbacktbl(id int,name varchar(10));
insert into saverollbacktbl(id,name) values(1, 'mike');
insert into saverollbacktbl(id,name) values(2, 'Jack');
delete  from saverollbacktbl where id =2;
savepoint tep;
insert into saverollbacktbl(id,name) values(2, 'Jack');
select * from saverollbacktbl;
rollback to savepoint tep;
select * from saverollbacktbl;
drop table saverollbacktbl;