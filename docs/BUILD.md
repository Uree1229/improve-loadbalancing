# 빌드와 소스 확인

이 저장소는 Linux 전체 트리가 아니라 변경 파일과 독립 패치를 보관한다. Linux 5.4.214 전체 소스와 대상 시스템에 맞는 커널 설정은 별도로 준비한다.

```bash
# 깨끗한 Linux 5.4.214 전체 소스 디렉터리에서 실행
git apply --check /path/to/improve-loadbalancing/versions/v15/changes.patch
git apply /path/to/improve-loadbalancing/versions/v15/changes.patch
make olddefconfig
make -j2
```

각 버전은 같은 기준에서 출발한다. v1 패치를 적용한 뒤 v2를 더하는 식으로 누적하지 않는다. 버전마다 별도 깨끗한 트리를 사용한다. 설치 및 부팅 설정은 대상 시스템에서 따로 결정한다.

## 비교용 C

```bash
cd benchmarks
make
./lb_bench 8 2086169180 256 bursty
make lb_affinity_probe
```

lb_bench는 작업자별 독립 버퍼에서 정해진 계산량을 실행한다. steady는 계속 계산하며 bursty는 계산 묶음 사이에 수면을 요청한다. 인자는 작업자 수, 전체 계산 steps, 작업자별 버퍼 KiB, 모드다. CPU affinity와 스케줄러 통계가 필요하므로 Linux에서 실행한다.

lb_affinity_probe는 affinity 제한과 해제 뒤 CPU 분산을 관찰하는 별도 실험 도구다. 자체 사용법과 전제 조건은 C 소스에 보존한다. 이는 원자적인 실제 CPU 점유 측정이 아니다.

## 패치와 스냅샷 일치 검사

```bash
python3 tools/verify_versions.py
```

이 검사는 각 패치를 공통 원본에 독립 적용한 결과와 버전별 소스 SHA256을 비교한다. 실제 커널 빌드나 부팅 검증을 대체하지 않는다.
