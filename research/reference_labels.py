"""Neutral display labels for research manifests; source URLs remain exact."""
import re


def neutral_title(title):
    title = re.sub(r'(?i)bitwig(?:\s*studio)?', 'Reference A', title)
    title = re.sub(r'(?i)cubase(?:\s*pro)?', 'Reference B', title)
    return re.sub(r'(Reference [AB])(?=[0-9])', r'\1 ', title)
