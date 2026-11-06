# Linux CPU Load Balancing Experiments

Linux **5.4.214**에서 affinity 제한을 풀었을 때 CPU 분산 복구를 앞당기기 위한 팀 프로젝트다. v1~v9의 변경 소스, 공통 기준에 대한 독립 패치, 비교용 C 및 결과 해석을 모았다.

현재 유망 후보는 **v9**다. 같은 이미지 OFF/ON 비교를 세 부팅·168회 수행해 사전 중앙값 기준을 통과했다. 원본 커널 대비 전체 채택은 아직 보류한다. 복구 24쌍 중 1쌍, 일반 작업 60쌍 중 10쌍에는 악화가 남았으므로 매 실행의 성능을 보장한다고 해석하지 않는다.

## 버전별 코드

| 버전 | 변경 | 상태 |
|---|---|---|
| [v1](versions/v1/) | newidle hotness 시간 2배 확대 | 초기 설계 이력 |
| [v2](versions/v2/) | source CFS runnable > 2일 때 확대 | 초기 설계 이력 |
| [v3](versions/v3/) | source CFS runnable > 1일 때 시간 절반 | 초기 설계 이력 |
| [v4](versions/v4/) | pinned backoff 상한 512에서 32로 축소 | 복구 단축, 재시도 증가로 채택 보류 |
| [v5](versions/v5/) | affinity 확장 때 NOHZ 요청 | 첫 효과 재현 실패, 채택 보류 |
| [v6](versions/v6/) | 대상 CPU irq_work와 로컬 복구 요청 | 복구 재현, 일반 비용 일부 실패 |
| [v7](versions/v7/) | 일반 루프 특수화와 cold handler 분리 | 복구 유지, 일반 비용 일부 실패 |
| [v8](versions/v8/) | 별도 tasklet으로 복구 경로 분리 | 복구 유지, steady와 고정 비용 실패 |
| [v9](versions/v9/) | 기본 OFF sysctl로 같은 이미지 교차 비교 | 3부팅 168회 중앙값 기준 통과, 원본 대비 채택 보류 |

각 폴더의 `changes.patch`는 **같은 기준 소스에 독립적으로** 적용한다. 패치를 순서대로 누적하지 않는다. `baseline/`은 변경 대상의 원본 파일이고 `versions/manifest.json`에 버전별 SHA256과 출처를 기록했다. v1~v4 소스는 보관된 원본과 패치로 복원했으며 v5~v9는 보존된 후보 소스와 일치한다.

## 읽는 순서

1. [전체 비교 문서](docs/TeamProject/CPU_load_balancing_overall_comparison_20261106.md) 또는 [9쪽 PDF](docs/CPU_load_balancing_overall_comparison_20261106.pdf)
2. [빌드와 비교 방법](docs/BUILD_AND_COMPARE.md)
3. [원본 비교용 C](loadbalance-v2/src/lb_bench.c)와 [팀 실행 안내](loadbalance-v2/TEAM_GUIDE.md)
4. [v9 세 부팅 결과](output/team_project/lb-v9-repro/RESULTS.md)

일반 작업의 조건별 짝 비율 중앙 변화는 -2.46~+3.21%, ON 복구 중앙값은 2.22~6.90 ms였다. 중앙값과 단일 실행 최댓값을 구분하고, v8의 기존 비용 기준 실패도 보존한다. 전체 원시 시행 파일과 큰 결과 압축은 이 코드 저장소에 포함하지 않았다.

## 배포 확인

`python tools/verify_versions.py`는 각 독립 패치를 공통 기준에 적용한 결과가 버전 소스와 같은지 검사한다. `SHA256SUMS.txt`는 보관한 모든 배포 파일의 원본 바이트 해시다. 커널 재빌드나 VM 실행을 대체하는 검사는 아니다.

라이선스와 출처는 [LICENSING.md](LICENSING.md)를 참고한다.
