# v6.1 동적 hot 실험

newly-idle에서 hot 임계값을 절반으로 축소. 기존 v6의 affinity 확장 대응을 유지한다.

`changes.patch`는 공통 `baseline/`에 독립 적용하는 전체 패치다. `changes-from-v6.patch`는 이미 v6인 소스에만 적용하는 차이 패치다. 둘 중 하나만 사용한다.

[측정 결과](../../experiments/v6-hot-separate-boots-20261123/) · [공통 빌드 안내](../../docs/BUILD.md)

복구 지연과 일반 작업 비용을 함께 일관되게 개선했다는 근거는 부족하다. 최종 채택된 개선판으로 표시하지 않는다.
