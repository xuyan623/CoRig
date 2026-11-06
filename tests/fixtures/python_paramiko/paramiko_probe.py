"""Paramiko-only probe used with the task-specified developer interpreter."""
import paramiko


def process(inputs, state):
    return {"paramiko": getattr(paramiko, "__version__", "")}
