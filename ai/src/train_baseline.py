"""Explicit rule-baseline interface; real thresholds require training data."""

from __future__ import annotations

from dataclasses import dataclass, fields
import math
from pathlib import Path

import yaml

from ai.src.features import FEATURE_VERSION, FeatureVector


FEATURE_NAMES = frozenset(field.name for field in fields(FeatureVector)) - {"feature_version"}


@dataclass(frozen=True)
class FeatureCondition:
    feature_name: str
    minimum: float | None = None
    maximum: float | None = None

    def validate(self) -> None:
        if self.feature_name not in FEATURE_NAMES:
            raise ValueError(f"unknown feature: {self.feature_name}")
        if self.minimum is None and self.maximum is None:
            raise ValueError("feature condition needs at least one bound")
        if self.minimum is not None and self.maximum is not None:
            if self.minimum > self.maximum:
                raise ValueError("feature condition minimum exceeds maximum")

    def matches(self, features: FeatureVector) -> bool:
        self.validate()
        value = float(getattr(features, self.feature_name))
        return (
            (self.minimum is None or value >= self.minimum)
            and (self.maximum is None or value <= self.maximum)
        )


@dataclass(frozen=True)
class BaselineRule:
    label: str
    conditions: tuple[FeatureCondition, ...]

    def validate(self, allowed_labels: set[str]) -> None:
        if self.label not in allowed_labels:
            raise ValueError(f"baseline rule label {self.label!r} is not allowed")
        if not self.conditions:
            raise ValueError("baseline rule must contain at least one condition")
        for condition in self.conditions:
            condition.validate()

    def matches(self, features: FeatureVector) -> bool:
        if not self.conditions:
            raise ValueError("baseline rule must contain at least one condition")
        return all(condition.matches(features) for condition in self.conditions)


@dataclass(frozen=True)
class RuleBaseline:
    rules: tuple[BaselineRule, ...]
    default_label: str = "unknown"

    def validate(self, allowed_labels: set[str]) -> None:
        if self.default_label not in allowed_labels:
            raise ValueError(
                f"baseline default label {self.default_label!r} is not allowed"
            )
        for rule in self.rules:
            rule.validate(allowed_labels)

    def predict(self, features: FeatureVector) -> str:
        if features.feature_version != FEATURE_VERSION:
            raise ValueError(
                f"feature version must be {FEATURE_VERSION}, got "
                f"{features.feature_version}"
            )
        for rule in self.rules:
            if rule.matches(features):
                return rule.label
        return self.default_label


def load_rule_baseline(path: Path) -> RuleBaseline:
    """Load an explicit baseline without inferring thresholds from test data."""

    raw = yaml.safe_load(path.resolve().read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("version") != 1:
        raise ValueError("dataset config version must be 1")
    labels_raw = raw.get("allowed_labels")
    if not isinstance(labels_raw, list) or not labels_raw:
        raise ValueError("dataset config allowed_labels must be a nonempty list")
    allowed_labels = {str(value) for value in labels_raw}
    baseline_raw = raw.get("baseline")
    if not isinstance(baseline_raw, dict):
        raise ValueError("dataset config baseline must be a mapping")
    if baseline_raw.get("feature_version") != FEATURE_VERSION:
        raise ValueError(f"baseline feature_version must be {FEATURE_VERSION}")
    rules_raw = baseline_raw.get("rules")
    if not isinstance(rules_raw, list):
        raise ValueError("baseline rules must be a list")

    rules: list[BaselineRule] = []
    for rule_index, rule_raw in enumerate(rules_raw):
        if not isinstance(rule_raw, dict):
            raise ValueError(f"baseline rule {rule_index} must be a mapping")
        conditions_raw = rule_raw.get("conditions")
        if not isinstance(conditions_raw, list):
            raise ValueError(
                f"baseline rule {rule_index} conditions must be a list"
            )
        conditions: list[FeatureCondition] = []
        for condition_index, condition_raw in enumerate(conditions_raw):
            if not isinstance(condition_raw, dict):
                raise ValueError(
                    f"baseline rule {rule_index} condition {condition_index} "
                    "must be a mapping"
                )
            feature_name = condition_raw.get("feature_name")
            if not isinstance(feature_name, str) or not feature_name:
                raise ValueError("baseline condition feature_name must be nonempty")
            conditions.append(FeatureCondition(
                feature_name=feature_name,
                minimum=_optional_finite_float(condition_raw.get("minimum")),
                maximum=_optional_finite_float(condition_raw.get("maximum")),
            ))
        rules.append(BaselineRule(
            label=str(rule_raw.get("label", "")),
            conditions=tuple(conditions),
        ))

    default_label = baseline_raw.get("default_label")
    if not isinstance(default_label, str) or not default_label:
        raise ValueError("baseline default_label must be nonempty")
    baseline = RuleBaseline(tuple(rules), default_label)
    baseline.validate(allowed_labels)
    return baseline


def _optional_finite_float(value: object) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool):
        raise ValueError("baseline condition bounds must be finite numbers or null")
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(
            "baseline condition bounds must be finite numbers or null"
        ) from error
    if not math.isfinite(result):
        raise ValueError("baseline condition bounds must be finite numbers or null")
    return result
