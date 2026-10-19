# Copyright Contributors to the OpenVDB Project
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import os,sys,json,hashlib,subprocess,statistics,platform,datetime,msvcrt,winreg,argparse
parser=argparse.ArgumentParser(description='Matched Windows native accessor and algorithm comparison')
parser.add_argument('--workspace',type=Path,required=True)
parser.add_argument('--stock-build',type=Path,required=True)
parser.add_argument('--cache-build',type=Path,required=True)
parser.add_argument('--stock-source',type=Path,required=True)
parser.add_argument('--cache-source',type=Path,required=True)
parser.add_argument('--tbb-root',type=Path,required=True)
parser.add_argument('--baseline-revision',default='84dca76d5759b0ca8d4d30aa473437630a357c4f')
parser.add_argument('--lock-file',type=Path,help='Existing shared host lock; otherwise use a workspace-local lock')
parser.add_argument('--cpu',type=int,default=16)
parser.add_argument('--tests',action='store_true')
args=parser.parse_args()
w=args.workspace.resolve()
if w==Path('D:/Workspaces/AI') or not w.is_relative_to(Path('D:/Workspaces/AI')): raise ValueError('Use a dedicated D:/Workspaces/AI subdirectory')
w.mkdir(parents=True,exist_ok=True)
(w/'temp').mkdir(exist_ok=True)
raw=w/'raw';raw.mkdir(exist_ok=True)
builds={'stock':args.stock_build.resolve(),'cache':args.cache_build.resolve()}
sources={'stock':args.stock_source.resolve(),'cache':args.cache_source.resolve()}
env=os.environ.copy()
env.update(TEMP=str(w/'temp'),TMP=str(w/'temp'),PYTHONDONTWRITEBYTECODE='1')
env['PATH']=str(args.tbb_root/'redist/intel64/vc14')+os.pathsep+env['PATH']
def save(path,obj): path.write_text(json.dumps(obj,indent=2)+'\n')
def run(cmd,cwd=w):
 p=subprocess.run([str(x) for x in cmd],cwd=cwd,env=env,capture_output=True,text=True,errors='replace')
 if p.returncode: raise RuntimeError(str(cmd)+'\n'+p.stdout[-3000:]+p.stderr[-3000:])
 return p.stdout
if args.tests:
 for variant in ['stock','cache']:
  cwd=w/('test-'+variant);cwd.mkdir(exist_ok=True)
  exe=builds[variant]/'openvdb/openvdb/unittest/Release/vdb_test.exe'
  print('Testing '+variant,flush=True)
  output=run([exe,'--gtest_output=json:'+str(raw/(variant+'-tests.json'))],cwd)
  (raw/(variant+'-tests.log')).write_text(output)
  print(output[-1500:],flush=True)
 sys.exit()
patterns=['singleleaf','coherent64','random64','coherent512','random512','partial64','collision64','root64','churn']
plan=dict(baseline=args.baseline_revision,types=['u16','float','double','int64'],patterns=patterns,
 launches=3,coherentLaunches=5,samples=5,coherentSamples=9,operations=8388608,coherentOperations=16777216,churnOperations=65536,
 warmupMilliseconds=500,coherentWarmupMilliseconds=1000,cpu=args.cpu,
 algorithmLaunches=5,algorithmSamples=5,algorithmOperations=1048576,algorithmWarmupCalls=2,denseCopiesPerSample=256,meanFilterIterationsPerSample=8,
 aggregation='Median of per-process sample medians; paired process ratios and full launch ranges retained. Descriptive shared-host comparison.',
 exclusions='No samples discarded. No profiler. Setup and validation outside timings; churn includes allocation/deallocation; accessor lifetime and clear costs timed explicitly.')
if (raw/'plan.json').exists(): raise RuntimeError('Immutable run exists')
save(raw/'plan.json',plan)
with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as k: cpu=winreg.QueryValueEx(k,'ProcessorNameString')[0]
metadata=dict(candidateRevision=run(['git','-C',sources['cache'],'rev-parse','HEAD']).strip(),candidateDirtyState=run(['git','-C',sources['cache'],'status','--short']).splitlines(),cpu=cpu,platform=platform.platform(),python=sys.version,started=datetime.datetime.now().astimezone().isoformat(),
 powerScheme=run(['powercfg','/getactivescheme']).strip(),tbb='2022.2.0',gtest='1.17.0',configuration='MSVC x64 Release /O2 /MT C++17; assertions enabled; __PRETTY_FUNCTION__=__FUNCSIG__ on both; no SIMD; no explicit instantiation; zlib enabled; Blosc disabled; delayed IO removed in baseline',
 sourceHashes={v:{f:hashlib.sha256((s/f).read_bytes()).hexdigest() for f in ['openvdb/openvdb/tree/ValueAccessor.h','openvdb/openvdb/Platform.h']} for v,s in sources.items()},
 harnessHashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__).with_name('main.cpp'),Path(__file__).with_name('algorithms.cpp'),Path(__file__)]},binaries={})
for v in ['stock','cache']:
 for p in list((builds[v]/'Release').glob('*.exe'))+[builds[v]/'openvdb/openvdb/Release/libopenvdb.lib']: metadata['binaries'][v+'/'+p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
ps=['powershell','-NoProfile','-Command','Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor | Select-Object Name,PercentProcessorTime | ConvertTo-Json -Compress']
metadata['cpuBefore']=json.loads(run(ps));save(raw/'environment.json',metadata)
lock_path=args.lock_file.resolve() if args.lock_file else w/'performance.lock'
with lock_path.open('r+b' if args.lock_file else 'a+b') as lock:
 lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
 runs=[]
 try:
  for kind in plan['types']:
   for pattern in patterns:
    launches=5 if pattern=='coherent64' else 3
    ops=16777216 if pattern=='coherent64' else 65536 if pattern=='churn' else 8388608
    for launch in range(launches):
     for variant in (['stock','cache'] if launch%2==0 else ['cache','stock']):
      exe=builds[variant]/'Release'/('cache_'+kind+'.exe')
      result=json.loads(run([exe,'--pattern',pattern,'--operations',ops,'--samples',9 if pattern=='coherent64' else 5,'--warmup-ms',1000 if pattern=='coherent64' else 500,'--write-first',launch%2,'--cpu',str(args.cpu)]))
      result.update(variant=variant,kind=kind,launch=launch+1); runs.append(result)
      save(raw/f'{kind}-{pattern}-{variant}-{launch+1}.json',result)
      print(kind,pattern,launch+1,variant,','.join(f"{x['name']}={statistics.median(x['elapsedMilliseconds'])*1e6/x['operations']:.3f}ns" for x in result['results']),flush=True)
  for launch in range(5):
   for variant in (['stock','cache'] if launch%2==0 else ['cache','stock']):
    data=json.loads(run([builds[variant]/'Release/algorithms.exe','1048576',str(args.cpu)]))
    data.update(variant=variant,kind='float',pattern='algorithms',launch=launch+1);runs.append(data)
    save(raw/f'algorithms-{variant}-{launch+1}.json',data)
    print('algorithms',launch+1,variant,flush=True)
  save(raw/'all-runs.json',runs)
 finally:
  lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
metadata['cpuAfter']=json.loads(run(ps));metadata['completed']=datetime.datetime.now().astimezone().isoformat();save(raw/'environment.json',metadata)
print('Complete; all samples and validation retained.',flush=True)
