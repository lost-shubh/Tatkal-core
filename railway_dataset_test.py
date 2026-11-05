import csv,sqlite3,tempfile
from pathlib import Path
import railway_dataset
with tempfile.TemporaryDirectory() as d:
    root=Path(d); (root/'trains.csv').write_text('number,name,type,runs_days,source,destination,distance,stop_count\n12001,  Demo  ,MEX,Daily, A , B ,100,2\n12001,Demo,MEX,Daily,A,B,100,2\nBAD,Bad,MEX,Daily,A,B,0,0\n',encoding='utf-8')
    (root/'stations.csv').write_text('code,name,lat,lon\n aa , Alpha,1,2\n',encoding='utf-8')
    (root/'stops.csv').write_text('train,sequence,station_code,station_name,day,arrival,departure,halt,distance\n12001,1,aa, Alpha ,1,08:00,08:05,5,0\n12001,2,,Beta,1,10:00,10:10,10,100\n',encoding='utf-8')
    out=root/'railway.db'; stats=railway_dataset.clean(root,out);db=sqlite3.connect(out)
    assert stats['trains_loaded']==2 and stats['stops_loaded']==2 and stats['rejected']==1
    assert db.execute('select name from trains where train_number=12001').fetchone()[0]=='Demo'
    assert db.execute('select station_code from stations').fetchone()[0]=='AA'
    db.close()
print('railway dataset cleaning test: passed')
