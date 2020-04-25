select  distinct 
    date_trunc('second', a.tm)::time without time zone as thetime,
    a.instance || ',' || a.channel || ',' || a.sdr as instance_channel_sdr,
    a.callsign,
    a.heardfrom,
    --a.manufacturer,
    --a.comment,
    round(cast(ST_Y(a.location2d) as numeric), 3) || ', ' || round(cast(ST_X(a.location2d) as numeric), 3) as coords,
    round(a.altitude,0) as "alt",
    round(cast(ST_DistanceSphere(ST_GeomFromText('POINT(-104.990278 39.739167)',4326), a.location2d)*.621371/1000 as numeric), 2) as "distance (mi) from Denver"

from
    dw_packets a

where 
    a.tm > (now() - time '00:05:00')
    and a.location2d != ''
    and a.freq = '144390000'
    and a.sdr = 0
    and a.callsign = a.heardfrom

order by 
    1 asc,
    2
;
