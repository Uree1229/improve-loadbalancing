# v6.5 동적 hot 실험

affinity 확장 후 원래 runqueue의 16ms 구간에서 idle 목적지에 hot 완화 적용. 기존 v6의 affinity 확장 대응을 유지한다.

`changes.patch`는 공통 `baseline/`에 독립 적용하는 전체 패치다. `changes-from-v6.patch`는 이미 v6인 소스에만 적용하는 차이 패치다. 둘 중 하나만 사용한다.

[측정 결과](RESULTS.md) · [공통 빌드 안내](../../docs/BUILD.md)

기본 정책은 0이며 추가 hot 완화는 꺼져 있다. 아래 인터페이스는 CONFIG_SMP, CONFIG_SCHED_DEBUG, CONFIG_DEBUG_FS가 켜진 실험 커널에서 사용한다. 실제 검증 대상은 x86_64였다.

| debugfs policy | 동작 |
|---|---|
| 0 | v6 정책, 기존 affinity kick 유지 |
| 1 | v6.2 정책 |
| 2 | v6.4 정책 |
| 3 | v6.5의 16ms 제한 완화 |

제어 위치는 `/sys/kernel/debug/sched_hot_v65/`이다. `policy`와 `diagnostics`는 root 전용이며, `stats`는 시스템 전체 판정 횟수를 출력한다. 성능 측정은 diagnostics=0, 별도 계측만 diagnostics=1로 진행했다. 정책 전환은 실행 중인 벤치마크가 없는 상태에서 한다. 계측 결과는 성공한 migration 횟수가 아니다.

**새 부팅 두 번의 512회 비교에서 목표를 입증하지 못해 채택을 보류했다.** 동일 이미지의 정책 대조이며 예전 별도 커널 바이너리끼리 비교한 결과와 합산하지 않는다.
