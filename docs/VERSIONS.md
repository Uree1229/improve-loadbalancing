# 버전별 변경

| 버전 | 코드에서 바꾼 동작 |
|---|---|
| [v1](../versions/v1/) | newidle hotness 시간 2배 확대 |
| [v2](../versions/v2/) | source CFS runnable > 2일 때 확대 |
| [v3](../versions/v3/) | source CFS runnable > 1일 때 시간 절반 |
| [v4](../versions/v4/) | pinned backoff 상한 512에서 32로 축소 |
| [v5](../versions/v5/) | affinity 확장 때 NOHZ 요청 |
| [v6](../versions/v6/) | 대상 CPU irq_work와 로컬 복구 요청 |
| [v7](../versions/v7/) | 일반 루프 특수화와 cold handler 분리 |
| [v8](../versions/v8/) | 별도 tasklet으로 복구 경로 분리 |
| [v9](../versions/v9/) | 기본 OFF sysctl로 같은 이미지 교차 비교 |
| [v10](../versions/v10/) | idle 상태가 이어질 때 제한된 재시도 |
| [v11](../versions/v11/) | idle CPU에서 적격 작업 하나를 가져오는 경로 |
| [v12](../versions/v12/) | idle 대상 CPU에 대한 원격 작업 이동 |
| [v13](../versions/v13/) | 실행 중인 CPU에서 강제 밸런싱을 건너뛰는 조건 |
| [v14](../versions/v14/) | 원격 이동 뒤 제한된 idle polling |
| [v15](../versions/v15/) | 원격 등록 뒤 대상 CPU의 깨우기 IPI를 제한적으로 재전송 |
| [v16](../versions/v16/) | idle 상태인 로컬 CPU에서 먼저 적격 작업을 가져오는 경로 |
| [v17](../versions/v17/) | idle 경로에서 작업 배치를 한 번 더 확인하는 제어 |
| [v18](../versions/v18/) | 작은 시스템에서 활성 CPU에 이미 작업이 배치된 경우 추가 알림 축소 |
| [v19](../versions/v19/) | v18 게스트에서 Windows 코어 파킹 해제 |
| [v20](../versions/v20/) | v18 게스트에서 Windows 코어 등급별 파킹 하한 조정 |
| [v21](../versions/v21/) | v19 설정에 장기 실행 스레드의 P코어 선호 추가 |
| [v22](../versions/v22/) | v21 설정에 두 코어 등급의 성능 우선 EPP 추가 |

v1~v3은 newly-idle 이동의 task hotness 판단을 조절한다. v4는 affinity 제한으로 이동이 막힌 뒤의 재시도 상한을 줄인다. v5 이후는 CPU 허용 범위가 넓어진 사건에 반응해 재분산을 요청한다. v6는 대상 CPU에 직접 알리고 v7은 일반 경로와 복구 경로를 분리한다. v8은 별도 tasklet을 사용하고 v9는 기본 OFF인 sysctl 제어를 추가한다.

모든 버전은 실험용이다. 소스가 같다는 검사는 실행 성능이나 모든 커널 동작의 안전성을 입증하지 않는다. 이 공개 저장소에는 실험 원자료와 호스트 및 VM 환경 기록을 포함하지 않는다.
