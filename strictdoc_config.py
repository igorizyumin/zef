"""StrictDoc project configuration for ZEF requirements traceability."""

from strictdoc.core.project_config import ProjectConfig


def create_config() -> ProjectConfig:
    return ProjectConfig(
        project_title="Z Embedded Framework",
        project_features=["REQUIREMENT_TO_SOURCE_TRACEABILITY"],
        include_source_paths=[
            "docs/**",
            "include/**",
            "src/**",
            "ports/**",
            "CMakeLists.txt",
            "strictdoc_config.py",
            "tests/**",
        ],
    )
