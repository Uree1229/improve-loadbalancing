# Linux CPU Load Balancing Experiments

Linux **5.4.214** 기반 CPU 로드밸런싱 개선 후보 **v1~v15**의 소스와 패치를 보관한다. CPU affinity 제한이 풀렸을 때 분산 복구를 앞당기는 접근과 task hotness 판단 변경을 다룬다.

## 버전별 코드

| 버전 | 변경 |
|---|---|
| [v1](versions/v1/) | newly-idle hotness 시간 기준 확대 |
| [v2](versions/v2/) | runnable 수 조건으로 적용 범위 축소 |
| [v3](versions/v3/) | runnable 작업이 있을 때 hotness 시간 기준 축소 |
| [v4](versions/v4/) | pinned backoff 상한 축소 |
| [v5](versions/v5/) | affinity 확장 때 NOHZ 요청 |
| [v6](versions/v6/) | 대상 CPU irq_work와 로컬 복구 요청 |
| [v7](versions/v7/) | 일반 루프 특수화와 cold handler 분리 |
| [v8](versions/v8/) | 별도 tasklet으로 복구 경로 분리 |
| [v9](versions/v9/) | 기본 OFF sysctl과 같은 이미지 내 기능 제어 |
| [v10](versions/v10/) | idle 상태가 이어질 때 제한된 재시도 |
| [v11](versions/v11/) | idle CPU에서 적격 작업 하나를 가져오는 경로 |
| [v12](versions/v12/) | idle 대상 CPU에 대한 원격 작업 이동 |
| [v13](versions/v13/) | 실행 중인 CPU에서 강제 밸런싱을 건너뛰는 조건 |
| [v14](versions/v14/) | 원격 이동 뒤 제한된 idle polling |
| [v15](versions/v15/) | 원격 등록 뒤 대상 CPU의 깨우기 IPI를 제한적으로 재전송 |

각 `changes.patch`는 같은 원본 기준에 독립 적용한다. 다른 버전 패치와 누적하지 않는다. `kernel/sched/` 폴더에는 해당 버전의 변경 파일을 보관하며 전체 커널은 포함하지 않는다.

- [코드 변경 설명](docs/VERSIONS.md)
- [빌드와 소스 확인](docs/BUILD.md)
- [비교용 C 프로그램](benchmarks/src/lb_bench.c)
- [affinity 복구 probe](benchmarks/src/lb_affinity_probe.c)
- [버전별 파일 해시](versions/manifest.json)

`baseline/`은 변경 대상 원본 파일이다. v1~v4는 보존된 원본과 패치에서 복원했고 v5~v15는 보존된 후보 소스와 일치한다. `python3 tools/verify_versions.py`로 모든 버전의 독립 패치와 스냅샷을 확인할 수 있다.

공개 범위는 버전별 코드, 패치, 일반 빌드 설명으로 한정한다. VM 디스크와 커널 바이너리, 실험 원자료, 로컬 경로 및 호스트 환경 기록, 자동 설치·권한 설정 도구는 포함하지 않는다. 실험 후보이므로 범용 성능 개선이나 최종 채택을 주장하지 않는다.

[소스 출처와 라이선스](LICENSING.md)를 참고한다. Linux 파일의 기존 저작권과 SPDX 표기를 유지한다.
